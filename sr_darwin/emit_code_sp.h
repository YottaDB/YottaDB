/****************************************************************
 *                                                              *
 * Copyright (c) 2007-2016 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017 YottaDB LLC. and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 * Copyright (c) 2017 Stephen L Johnson. All rights reserved.	*
 *                                                              *
 *      This source code contains the intellectual property     *
 *      of its copyright holder(s), and is made available       *
 *      under a license.  If you do not know the terms of       *
 *      the license, please stop and do not read further.       *
 *                                                              *
 ****************************************************************/

#include "mdef.h"
#include "x86_64.h"

/* Some general comments on code generation for x86-64:
 *
 * 1.  Code generation is a fairly simple subset of what is possible with X86-64. For example, GT.M never
 *     generates memory-to-memory instructions. Where necessary, values are loaded to a register, then stored.
 * 2.  Only one use of the REP prefix is with the MOVSQ instructions (MOVSXD with REX bit to make it MOVSQ) used
 *     for copying MVALs inline in generatede code.
 * 3.  As noted below, there are several constructs building offsets that come up with different sized offsets
 *     between some of the compiler phases. Consequently, we force offsets to be 32bit for certain classes of memory
 *     references. This should be looked at again so we can eliminate the 4 byte "override" as it were which would
 *     again cause a significant decrease in generated code size.
 */
#ifdef DEBUG
void format_machine_inst(void);
void initialize_registers(void);
void reset_instruction(void);
void print_source_operand(void);
void print_destination_operand(void);
void print_instruction(void);
void set_memory_reg(void);
void set_register_reg(void);
void clear_memory_reg(void);
#endif

void emit_base_offset(int base, int offset);

#define EMIT_BASE_OFFSET_ADDR(base, offset)		emit_base_offset(base, offset)
#define EMIT_BASE_OFFSET_LOAD(base, offset)		emit_base_offset(base, offset)
#define EMIT_BASE_OFFSET_EITHER(base, offset, inst)	emit_base_offset(base, offset)

/*  Define the COND values for use in the 2 byte JCC instructions.. */
#define INST_SIZE 	1
#define XFER_BYTE_INST_SIZE 3
#define XFER_LONG_INST_SIZE 6
#define BRB_INST_SIZE 2
#define JMP_LONG_INST_SIZE 5
#define CALL_4LCLDO_XFER 2		/* Index in ttt from start of call[sp] and forlcldo to xfer_table index */
#define MAX_BRANCH_CODEGEN_SIZE 32	/* The length in bytes, of the longest form of branch instruction sequence */

#define REX_FIELD 	char

#define REX_OP	0x40
#define REX_B 	0x01
#define REX_X	0x02
#define REX_R 	0x04
#define REX_W	0x08

/* Size of the MVAL in quadwords (64 bits on x86_64) - used when copying mval */
#define MVAL_QWORD_SIZE	(SIZEOF(mval) / SIZEOF(long))

/* Macro used to clear the flags in emit_base_info.flags. Using this method rather than declaring a union and complicating
 * the reference we make (and gendefinedtypesinit.csh doesn't like unnamed unions/structs at this point 7/2016). Note since
 * we no longer clear the other parts of the structure with memset(), we do need to explicitly zero out the rex field
 * since it is assumed 0 after a clear and flags are OR'd into it.
 */
#define CLEAR_EMIT_BASE_INFO				\
MBSTART {						\
	*((char *)(&emit_base_info.flags)) = 0;		\
	emit_base_info.rex = 0;				\
} MBEND

/* Note that while the register number we use in gtm can go from 0-15, the number of bits in the modrm/sib regfields are only 3.
 * So if we need to use the regs r8-r15, then a bit in the REX field should be set (depending on whether the register is encoded
 * in the sib or opcode or modrm, etc).
 */
#define SET_REX_PREFIX(REX_MANDATORY, REX_COND, reg)							\
MBSTART { 												\
	if (reg & 0x8)	/* If bit 3 (index starting from 0) is set, set the specified REX bit */	\
		emit_base_info.rex |= REX_COND;								\
	emit_base_info.rex |= REX_OP | REX_MANDATORY;							\
	emit_base_info.flags.rex_set = TRUE;								\
} MBEND

int x86_64_arg_reg(int indx);
#define GET_ARG_REG(indx)	x86_64_arg_reg(indx)

/* Should be offset from RSP */
#define STACK_ARG_OFFSET(indx)	(8 * indx)

/* Define the macros for the instructions to be generated.. */
#define GENERIC_OPCODE_BEQ		1
#define GENERIC_OPCODE_BGE		2
#define GENERIC_OPCODE_BGT		3
#define GENERIC_OPCODE_BLE		4
#define GENERIC_OPCODE_BLT		5
#define GENERIC_OPCODE_BNE		6
#define GENERIC_OPCODE_BLBC		7
#define GENERIC_OPCODE_BLBS		8
#define GENERIC_OPCODE_BR		9
#define GENERIC_OPCODE_LDA		11
#define GENERIC_OPCODE_LOAD		12
#define GENERIC_OPCODE_STORE		13
#define GENERIC_OPCODE_STORE_ZERO	14
#define GENERIC_OPCODE_NOP		15

#define LONG_JUMP_OFFSET 		(0x7ffffffc)
#define MAX_OFFSET			0xffffffff
#define EMIT_JMP_ADJUST_BRANCH_OFFSET	branch_offset -= 5

/* Note: the code base contains an odd assortment of unused features of the x86-64 architecture. Many of these
 * unused features are not fully implemented but for the pieces that are implemented, where testing for them causes
 * us to spend time doing unnecessary IF tests or clearing unnecessary storage (such as in emit_base_info struct),
 * those features are commented out rather than removed in case they are needed in the future.
 */
#define EMIT_BASE_CODE_SIZE ((emit_base_info.flags.rex_set ? 1 : 0) /* REX */ + 1 /* OPCODE */ 			\
	+ (emit_base_info.flags.modrm_byte_set ? 1 : 0)  /* MODRM */						\
	+ (emit_base_info.flags.sib_byte_set ? 1 : 0) + (emit_base_info.flags.offset8_set ? 1 : 0)		\
	+ (emit_base_info.flags.offset32_set ? 4 : 0)								\
	+ (emit_base_info.flags.imm32_set ? 4 : 0)								\
	/* + (emit_base_info.flags.offset64_set ? 8 : 0) + not used */						\
	/* + (emit_base_info.flags.imm8_set ? 1 : 0) not used */						\
	/* + (emit_base_info.flags.imm64_set ? 8 : 0) not used */)

/* Generate a memory reference using flags/values in emit_base_info structre. Note some field references
 * have been commented out as GT.M does not currently use them (so no use testing for them). If these
 * types become useful (and fully implemented), this/these line(s) can be uncommented.
 */
#define CODE_BUF_GEN(op_code)								\
MBSTART {										\
	(emit_base_info.flags.rex_set) ? (code_buf[code_idx++] = emit_base_info.rex):0;	\
	code_buf[code_idx++] = (char)op_code;						\
	if (emit_base_info.flags.modrm_byte_set)					\
		code_buf[code_idx++] = emit_base_info.modrm_byte.byte;			\
	if (emit_base_info.flags.sib_byte_set)						\
		code_buf[code_idx++] = emit_base_info.sib_byte.byte;			\
	if (emit_base_info.flags.offset8_set)						\
		code_buf[code_idx++] = emit_base_info.offset8;				\
	else if (emit_base_info.flags.offset32_set)					\
	{										\
		*((int4 *)&code_buf[code_idx]) = emit_base_info.offset32;		\
		code_idx += SIZEOF(int4);						\
	}										\
	/* else if (emit_base_info.offset64_set)	Not currently used	*/	\
	/* {									*/	\
	/*	*((int64_t *)&code_buf[code_idx]) = emit_base_info.offset64; 	*/	\
	/*	code_idx += SIZEOF(int64_t);					*/	\
	/* }									*/	\
	/* if (emit_base_info.flags.imm8_set)					*/	\
	/*	code_buf[code_idx++] = emit_base_info.imm8;			*/	\
	/* else 								*/	\
	if (emit_base_info.flags.imm32_set)						\
	{										\
		*((int4 *)&code_buf[code_idx]) = emit_base_info.imm32;			\
		code_idx += SIZEOF(int4);						\
	}										\
	/* else if (emit_base_info.imm64_set)			       		*/	\
	/* {									*/	\
	/* 	*((int64_t *)&code_buf[code_idx]) = emit_base_info.imm64;	*/	\
	/* 	code_idx += SIZEOF(int64_t);					*/	\
	/* }									*/	\
} MBEND

#define IGEN_LOAD_WORD_REG_8(reg)					\
MBSTART {								\
	SET_REX_PREFIX(REX_W, REX_R, reg);				\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;		\
	CODE_BUF_GEN(I386_INS_MOV_Gv_Ev);				\
} MBEND
/* Only used immediately after call to emit_base_offset() which does a CLEAR_EMIT_BASE_INFO macro */
#define IGEN_LOAD_NATIVE_REG(reg)	IGEN_LOAD_WORD_REG_8(reg)

/* Sign extended load */
#define IGEN_LOAD_WORD_REG_4(reg)					\
MBSTART {								\
	SET_REX_PREFIX(REX_W, REX_R, reg);				\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;		\
	CODE_BUF_GEN(I386_INS_MOVSXD_Gv_Ev);				\
} MBEND

#define IGEN_STORE_WORD_REG_8(reg)					\
MBSTART {								\
	SET_REX_PREFIX(REX_W, REX_R, reg);				\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;		\
	CODE_BUF_GEN(I386_INS_MOV_Ev_Gv);				\
} MBEND

#define IGEN_STORE_WORD_REG_4(reg)					\
MBSTART {								\
	SET_REX_PREFIX(0, REX_R, reg);					\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;		\
	CODE_BUF_GEN(I386_INS_MOV_Ev_Gv);				\
} MBEND

#define IGEN_STORE_ZERO_REG_8(reg)				\
MBSTART {							\
	SET_REX_PREFIX(REX_W, 0, 0);				\
	emit_base_info.imm32 = 0;				\
	emit_base_info.flags.imm32_set = TRUE;			\
	CODE_BUF_GEN(I386_INS_MOV_Ev_Iv);			\
} MBEND

#define IGEN_STORE_ZERO_REG_4(reg)				\
MBSTART {							\
	emit_base_info.imm32 = 0;				\
	emit_base_info.flags.imm32_set = TRUE;			\
	CODE_BUF_GEN(I386_INS_MOV_Ev_Iv);			\
} MBEND

#define IGEN_GENERIC_REG(opcode, reg)				\
MBSTART {							\
	switch(opcode)						\
	{							\
		case GENERIC_OPCODE_LDA:			\
			IGEN_LOAD_ADDR_REG(reg);		\
			break;					\
		case GENERIC_OPCODE_LOAD:			\
			if (4 == next_ptr_offset)		\
				IGEN_LOAD_WORD_REG_4(reg);	\
			else					\
				IGEN_LOAD_WORD_REG_8(reg);	\
			next_ptr_offset = 8;			\
			break;					\
		case GENERIC_OPCODE_STORE:			\
			if (4 == next_ptr_offset)		\
				IGEN_STORE_WORD_REG_4(reg);	\
			else					\
				IGEN_STORE_WORD_REG_8(reg);	\
			next_ptr_offset = 8;			\
			break;					\
		case GENERIC_OPCODE_STORE_ZERO:			\
			if (4 == next_ptr_offset)		\
				IGEN_STORE_ZERO_REG_4(reg);	\
			else					\
				IGEN_STORE_ZERO_REG_8(reg);	\
			next_ptr_offset = 8;			\
			break;					\
		default: /* which opcode ? */			\
			assertpro(FALSE);			\
			break;					\
	}							\
} MBEND

/* Only used immediately after call to emit_base_offset() which does a CLEAR_EMIT_BASE_INFO macro */
#define IGEN_LOAD_LINKAGE(reg)	 	IGEN_LOAD_WORD_REG_8(reg)

#define IGEN_LOAD_ADDR_REG(reg)					\
MBSTART {							\
	SET_REX_PREFIX(REX_W, REX_R, reg);			\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;	\
	CODE_BUF_GEN(I386_INS_LEA_Gv_M);			\
} MBEND

#define GEN_STORE_ARG(reg, offset)				\
MBSTART {							\
	X86_64_ONLY(force_32 = TRUE;)				\
	GEN_STORE_WORD_8(reg, I386_REG_RSP, offset);		\
	X86_64_ONLY(force_32 = FALSE;)				\
} MBEND

#define GEN_LOAD_WORD_8(reg, base_reg, offset)			\
MBSTART {							\
	emit_base_offset(base_reg, offset);			\
	SET_REX_PREFIX(REX_W, REX_R, reg);			\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;	\
	CODE_BUF_GEN(I386_INS_MOV_Gv_Ev);			\
} MBEND

/* Load 4 bytes with sign extension */
#define GEN_LOAD_WORD_4(reg, base_reg, offset)			\
MBSTART {							\
	emit_base_offset(base_reg, offset);			\
	SET_REX_PREFIX(REX_W, REX_R, reg);			\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;	\
	CODE_BUF_GEN(I386_INS_MOVSXD_Gv_Ev);			\
} MBEND

#define GEN_STORE_WORD_8(reg, base_reg, offset)			\
MBSTART {							\
	emit_base_offset(base_reg, offset);			\
	SET_REX_PREFIX(REX_W, REX_R, reg);			\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;	\
	CODE_BUF_GEN(I386_INS_MOV_Ev_Gv);			\
} MBEND

#define GEN_STORE_WORD_4(reg, base_reg, offset)			\
MBSTART {							\
	emit_base_offset(base_reg, offset);			\
	SET_REX_PREFIX(0, REX_R, reg);				\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;	\
	CODE_BUF_GEN(I386_INS_MOV_Ev_Gv);			\
} MBEND

#define GEN_LOAD_IMMED(reg, imval)				\
MBSTART {							\
	CLEAR_EMIT_BASE_INFO;					\
	SET_REX_PREFIX(0, REX_B, reg);				\
	emit_base_info.imm32 = imval & 0xffffffff;		\
	emit_base_info.flags.imm32_set = TRUE;			\
	CODE_BUF_GEN(I386_INS_MOV_eAX + (reg & 0x7));		\
} MBEND

#define GEN_CLEAR_WORD_EMIT(reg)	emit_trip(*(fst_opr + *inst++), TRUE, GENERIC_OPCODE_STORE_ZERO, reg)

#define GEN_LOAD_WORD_EMIT(reg)		emit_trip(*(fst_opr + *inst++), TRUE, GENERIC_OPCODE_LOAD, reg)

#define EMIT_TRIP_ILIT_GEN		GEN_LOAD_IMMED(trg_reg, immediate)

#define GEN_XFER_TBL_CALL(xfer)											\
MBSTART {													\
	emit_base_offset(GTM_REG_XFER_TABLE, xfer);								\
	emit_base_info.flags.rex_set = FALSE;									\
	assert(7 >= GTM_REG_XFER_TABLE); /* if its a 64 bit register, we might need to set the REX bit */	\
	emit_base_info.modrm_byte.modrm.reg_opcode = (char)I386_INS_CALL_Ev;					\
	CODE_BUF_GEN(I386_INS_Grp5_Prefix);									\
} MBEND

#define GEN_CMP_EAX_IMM32(imm)				\
MBSTART {						\
	CLEAR_EMIT_BASE_INFO;				\
	emit_base_info.imm32 = (int4)imm & 0xffffffff;	\
	emit_base_info.flags.imm32_set = TRUE;		\
	CODE_BUF_GEN(I386_INS_CMP_eAX_Iv);		\
} MBEND

#define GEN_CMP_MEM64_ZERO(base_reg, offset)				\
MBSTART {								\
	emit_base_offset(base_reg, offset);				\
	SET_REX_PREFIX(REX_W, 0, 0);					\
	emit_base_info.modrm_byte.modrm.reg_opcode = I386_INS_CMP__;	\
	emit_base_info.imm32 = 0;					\
	emit_base_info.flags.imm32_set = TRUE;				\
	CODE_BUF_GEN(I386_INS_Grp1_Ev_Iv_Prefix);			\
} MBEND

#define GEN_CMP_MEM32_ZERO(base_reg, offset)				\
MBSTART {								\
	emit_base_offset(base_reg, offset);				\
	emit_base_info.modrm_byte.modrm.reg_opcode = I386_INS_CMP__;	\
	emit_base_info.imm32 = 0;					\
	emit_base_info.flags.imm32_set = TRUE;				\
	CODE_BUF_GEN(I386_INS_Grp1_Ev_Iv_Prefix);			\
} MBEND

#define GEN_CMP_REG_MEM32(reg, base_reg, offset)			\
MBSTART {								\
	emit_base_offset(base_reg, offset);				\
	SET_REX_PREFIX(0, REX_R, reg);					\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;		\
	CODE_BUF_GEN(I386_INS_CMP_Gv_Ev);				\
} MBEND

#define GEN_CMP_REG_MEM64(reg, base_reg, offset)			\
MBSTART {								\
	emit_base_offset(base_reg, offset);				\
	SET_REX_PREFIX(REX_W, REX_R, reg);				\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg & 0x7;		\
	CODE_BUF_GEN(I386_INS_CMP_Gv_Ev);				\
} MBEND

#define GEN_CMP_REGS(reg1, reg2)					\
MBSTART {								\
	CLEAR_EMIT_BASE_INFO;						\
	SET_REX_PREFIX(REX_W, REX_R, reg1);				\
	SET_REX_PREFIX(0, REX_B, reg2);					\
	emit_base_info.modrm_byte.modrm.reg_opcode = reg1 & 0x7;	\
	emit_base_info.modrm_byte.modrm.mod = I386_MOD32_REGISTER;	\
	emit_base_info.modrm_byte.modrm.r_m = reg2 & 0x7;		\
	emit_base_info.flags.modrm_byte_set = TRUE;			\
	CODE_BUF_GEN(I386_INS_CMP_Gv_Ev);				\
} MBEND

/* Note that there is no CMP_IMM64 in AMD64 */
#define GEN_CMP_IMM32(reg, imm)						\
MBSTART {								\
	CLEAR_EMIT_BASE_INFO;						\
	emit_base_info.modrm_byte.modrm.reg_opcode = I386_INS_CMP__;	\
	emit_base_info.modrm_byte.modrm.mod = I386_MOD32_REGISTER;	\
	emit_base_info.modrm_byte.modrm.r_m = reg & 0x7;		\
	emit_base_info.flags.modrm_byte_set = TRUE;			\
	SET_REX_PREFIX(0, REX_B, reg);					\
	emit_base_info.imm32 = imm & 0xffffffff;			\
	emit_base_info.flags.imm32_set = TRUE;				\
	CODE_BUF_GEN(I386_INS_Grp1_Ev_Iv_Prefix);			\
} MBEND

#define GEN_SUBTRACT_REGS(src1, src2, trgt) assertpro(FALSE);

#define GEN_ADD_IMMED(reg, imval)					\
MBSTART {								\
	CLEAR_EMIT_BASE_INFO;						\
	SET_REX_PREFIX(REX_W, REX_B, reg);				\
	emit_base_info.flags.modrm_byte_set = TRUE;			\
	emit_base_info.modrm_byte.modrm.mod = I386_MOD32_REGISTER;	\
	emit_base_info.modrm_byte.modrm.reg_opcode = I386_INS_ADD__;	\
	emit_base_info.modrm_byte.modrm.r_m = reg & 0x7;		\
	emit_base_info.imm32 = imval;					\
	emit_base_info.flags.imm32_set = TRUE;				\
	CODE_BUF_GEN(I386_INS_Grp1_Ev_Iv_Prefix);			\
} MBEND

#define GEN_MOVE_REG(trg, src)						\
MBSTART {								\
	CLEAR_EMIT_BASE_INFO;						\
	SET_REX_PREFIX(REX_W, REX_B, trg);				\
	SET_REX_PREFIX(0, REX_R, src);					\
	emit_base_info.flags.modrm_byte_set = TRUE;			\
	emit_base_info.modrm_byte.modrm.mod = I386_MOD32_REGISTER;	\
	emit_base_info.modrm_byte.modrm.reg_opcode = src;		\
	emit_base_info.modrm_byte.modrm.r_m = trg & 0x7;		\
	CODE_BUF_GEN(I386_INS_MOV_Ev_Gv);				\
} MBEND

#define GEN_JUMP_REG(reg)						\
MBSTART {								\
	CLEAR_EMIT_BASE_INFO;						\
	SET_REX_PREFIX(REX_OP | REX_W, REX_B, reg);			\
	emit_base_info.flags.modrm_byte_set = TRUE;			\
	emit_base_info.modrm_byte.modrm.mod = I386_MOD32_REGISTER;	\
	emit_base_info.modrm_byte.modrm.reg_opcode = I386_INS_JMP_Ev;	\
	emit_base_info.modrm_byte.modrm.r_m = reg & 0x7;		\
	CODE_BUF_GEN(I386_INS_Grp5_Prefix);				\
} MBEND

#define GEN_PCREL							\
MBSTART {								\
	CLEAR_EMIT_BASE_INFO;						\
	emit_base_info.imm32 = 0;					\
	emit_base_info.flags.imm32_set = TRUE;				\
	CODE_BUF_GEN(I386_INS_CALL_Jv);					\
	CLEAR_EMIT_BASE_INFO;						\
	SET_REX_PREFIX(0, REX_B, GTM_REG_CODEGEN_TEMP);			\
	assert(7 < GTM_REG_CODEGEN_TEMP);				\
	CODE_BUF_GEN(I386_INS_POP_eAX + (GTM_REG_CODEGEN_TEMP & 0x7));	\
} MBEND

#define GEN_MVAL_COPY(SRC_REG, TRG_REG, QWSIZE)										\
MBSTART {														\
	GEN_LOAD_IMMED(I386_REG_ECX, QWSIZE);										\
	/* Do this brute force instead of using SET_REX_PREFIX/CODE_BUF_GEN() macros since we'd have to add a new	\
	 * test to CODE_BUF_GEN() macros to support F3H prefix we need only for the MOVSQ instruction. The instruction	\
	 * format is REP header followed by the REX field used to turn the MOVSW instruction into a MOVSQ and finally   \
	 * the instruction opcode itself. There are no (express) parameters as this instruction used pre-determined 	\
	 * registers (ARG1:src, ARG0:trg, ARG3:len-in-qwords since this is a MOVSQ).					\
	 */														\
	code_buf[code_idx++] = I386_INS_REP_E_Prefix;									\
	code_buf[code_idx++] = REX_OP | REX_W;										\
	code_buf[code_idx++] = I386_INS_MOVSW_D_Xv_Yv;	/* REX.W prefix turns this to MOVSQ */				\
} MBEND

/*
 * GT.M on AIX and SPARC is 64bit
 * By default the loads/stores use ldd/std(load double),
 * but if the value being dealt with is a word, the
 * opcode in generic_inst is changed to ldw/stw
 * On other platforms, it is defined to null
 */
#define REVERT_GENERICINST_TO_WORD(inst)

enum condition
{
	JO,
	JNO,
	JB,
	JC = JB,
	JNAE = JC,
	JNB,
	JNC = JNB,
	JAE = JNB,
	JZ,
	JE = JZ,
	JNZ,
	JNE = JNZ,
	JBE,
	JNA = JBE,
	JNBE,
	JA = JNBE,
	JS,
	JNS,
	JP,
	JPE = JP,
	JNP,
	JPO = JNP,
	JL,
	JNGE = JL,
	JNL,
	JGE = JNL,
	JLE,
	JNG = JLE,
	JNLE,
	JG = JNLE
};

typedef union
{
	ModR_M		modrm;
	unsigned char	byte;
} modrm_byte_type;

typedef union
{
	SIB		sib;
	unsigned char	byte;
} sib_byte_type;

/* Instead of filling up the code buffer, emit_base_offset() instead fills this structure, which is used by the
 * CODE_BUF_GEN macro to actually create the generated instruction stream. Note we use bit flags in this structure
 * to keep it as small as possible since it gets reset (cleared via memset) for nearly every instruction generated.
 */
typedef struct
{
	modrm_byte_type	modrm_byte;
	sib_byte_type	sib_byte;
	REX_FIELD	rex;
	char		offset8;
	struct
	{	/* Type needs to change if exceed 8 (used) flag bits */
		unsigned char	rex_set: 1;
		unsigned char	modrm_byte_set : 1;
		unsigned char	sib_byte_set : 1;
		unsigned char	offset8_set : 1;
		unsigned char	offset32_set : 1;
		unsigned char	imm32_set : 1;
		/* unsigned char imm8_set : 1;		Not currently used */
		/* unsigned char offset64_set : 1;	Not currently used */
		/* unsigned char imm64_set : 1;		Not currently used */
	} flags;
	/* char		imm8;				Not currently used */
	char		filler;
	int		offset32;
	int		imm32;
	/* int64_t		offset64;
	 * int64_t		imm64;			These fields not currently used
	 */
} emit_base_info_struct;
