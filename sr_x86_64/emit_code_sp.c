/****************************************************************
 *								*
 * Copyright (c) 2007-2016 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2018 YottaDB LLC. and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include <sys/types.h>
#include "gtm_string.h"
#include <errno.h>
#include "gtm_fcntl.h"
#include "gtm_stat.h"
#include "gtm_stdio.h"
#include "opcode.h"
#include "mdq.h"
#include <rtnhdr.h>
#include "vxi.h"
#include "vxt.h"
#include "cgp.h"
#include "compiler.h"
#include "list_file.h"
#include "emit_code.h"

#define SET_OBPT_STR(str, len)	(memcpy(obpt, str, len), obpt += len)
#define SET_OBPT_INT4(value)	obpt = i2asc(obpt, value)
#define SET_OBPT_INT8(value)	obpt = i2asclx(obpt, value)

/* Possible values for instruction Byte's Meaning */
#define one_byte_opcode		0
#define two_byte_opcode		1
#define modrm_sib_bytes		2
#define one_byte_immediate	3
#define double_word_immediate	4
#define quad_word_immediate	5
#define one_byte_offset		6
#define double_word_offset 	7
#define quad_word_offset	8

/* Now Define The Instruction Structure .... */
#define grp_prefix	4
/* flags to be used by "operand_class" */
#define undefined_class 0
#define register_class	1
#define memory_class 	2
#define immediate_class	3

/* Decoding instruction prefix indicating 64 bit features used */
struct Rex
{
	short	Base;
	/* short	Index;		Not currently used */
	short	Reg;
	short	Word64;
} rex_prefix;

struct instruction_mnemonics
{
	char		*opcode_mnemonic;
	char		opcode_suffix;
	char		reg_prefix;
	boolean_t	reg_rip;
	int		num_operands;	/* Instructions have 0, 1, or 2 operands. One operand is taken to be a source operand.
					 * Four operands would mean that modrm reg_opcode will denote opcode extension.
					 */
	int		source_operand_class;
	char		*source_operand_reg;
	int		destination_operand_class;
	int		has_immediate;
	char		*destination_operand_reg;
	long		offset;
	long		immediate;
} instruction;

#undef I386_OP
#define I386_OP(opcode, operand, num)	#opcode ,
#define REG_RIP 16

LITDEF char *mnemonic_list[] = {
	#include "i386_ops.h"
	};
LITDEF char *mnemonic_list_2b[] = {
	#include "i386_ops_2b.h"
	};
LITDEF char *mnemonic_list_g1[] = {
	#include "i386_ops_g1.h"
	};
LITDEF char *mnemonic_list_g2[] = {
	#include "i386_ops_g2.h"
	};
LITDEF char *mnemonic_list_g3[] = {
	#include "i386_ops_g3.h"
	};
LITDEF char *mnemonic_list_g4[] = {
	#include "i386_ops_g4.h"
	};
LITDEF char *mnemonic_list_g5[] = {
	#include "i386_ops_g5.h"
	};
LITDEF char *register_list[] = {
	"AX",
	"CX",
	"DX",
	"BX",
	"SP",
	"BP",
	"SI",
	"DI",
	"8",
	"9",
	"10",
	"11",
	"12",
	"13",
	"14",
	"15",
	"RIP"
};

GBLDEF int			instidx, prev_idx;

GBLREF emit_base_info_struct	emit_base_info;
GBLREF int 			call_4lcldo_variant;
GBLREF int			jmp_offset;	/* Offset to jump target */
GBLREF char			cg_phase;	/* Code generation phase */
GBLREF CODE_TYPE		code_buf[];
GBLREF int			code_idx;
GBLREF unsigned char		*obpt;	 	/* Output buffer index */
GBLREF unsigned char		outbuf[];
GBLREF int			curr_addr;
GBLREF boolean_t		force_32;	/* We want to generate 4 byte offets even for an offset lesser than 8bits long,
						 * to keep things consistent between CGP_APPROX_ADDR phase and CGP_MACHINE phase.
						 */
static modrm_byte_type modrm_byte;
static sib_byte_type sib_byte;


int	x86_64_arg_reg(int indx)
{
	switch(indx)
	{
		case 0:
			return I386_REG_RDI;
		case 1:
			return I386_REG_RSI;
		case 2:
			return I386_REG_RDX;
		case 3:
			return I386_REG_RCX;
		case 4:
			return I386_REG_R8;
		case 5:
			return I386_REG_R9;
		default:
			assertpro(FALSE);
			break ;
	}
	/* Control will never reach here */
	return -1 ;
}

void	emit_jmp(uint4 branch_op, short **instp, int reg) /* Note that the 'reg' parameter is ignored */
{
	assert (jmp_offset != 0);
	force_32 = TRUE;
	jmp_offset -= code_idx * SIZEOF(code_buf[0]);	/* size of this particular instruction */
	switch (cg_phase)
	{
#		ifdef DEBUG
		case CGP_ASSEMBLY:
			*obpt++ = 'x';
			*obpt++ = '^';
			*obpt++ = '0';
			*obpt++ = 'x';
			obpt += i2hex_nofill(jmp_offset, (uchar_ptr_t)obpt, 8);
			*obpt++ = ',';
			*obpt++ = ' ';
		/***** WARNING - FALL THRU *****/
#		endif
		case CGP_ADDR_OPT:
		case CGP_APPROX_ADDR:
		case CGP_MACHINE:
			assert (**instp == VXT_JMP);
			*instp += 1;
			assert (**instp == 1);
			*instp += 1;
			if (jmp_offset == 0)
			{
				/*code_buf[code_idx++] = I386_INS_NOP__; */
			} else if (((jmp_offset - 2) >= -128 && (jmp_offset - 2) <= 127 &&
				    JMP_LONG_INST_SIZE != call_4lcldo_variant) && (force_32 == FALSE))
			{
				jmp_offset -= 2;
				switch (branch_op)
				{
					case GENERIC_OPCODE_BEQ:
						code_buf[code_idx++] = I386_INS_JZ_Jb;
						break;
					case GENERIC_OPCODE_BGE:
						code_buf[code_idx++] = I386_INS_JNL_Jb;
						break;
					case GENERIC_OPCODE_BGT:
						code_buf[code_idx++] = I386_INS_JNLE_Jb;
						break;
					case GENERIC_OPCODE_BLE:
						code_buf[code_idx++] = I386_INS_JLE_Jb;
						break;
					case GENERIC_OPCODE_BLT:
						code_buf[code_idx++] = I386_INS_JL_Jb;
						break;
					case GENERIC_OPCODE_BNE:
						code_buf[code_idx++] = I386_INS_JNZ_Jb;
						break;
					case GENERIC_OPCODE_BR:
						assert(0 == call_4lcldo_variant || BRB_INST_SIZE == call_4lcldo_variant);
						code_buf[code_idx++] = I386_INS_JMP_Jb;
						break;
					default:
						assertpro(FALSE);
						break;
				}
				code_buf[code_idx++] = jmp_offset & 0xff;
			} else
			{
				if (branch_op == GENERIC_OPCODE_BR)
				{
					assert(0 == call_4lcldo_variant || JMP_LONG_INST_SIZE == call_4lcldo_variant || force_32);
					jmp_offset -= SIZEOF(int4) + 1;
					code_buf[code_idx++] = I386_INS_JMP_Jv;
				} else
				{
					jmp_offset -= SIZEOF(int4) + 2;
					code_buf[code_idx++] = I386_INS_Two_Byte_Escape_Prefix;
					switch (branch_op)
					{
						case GENERIC_OPCODE_BEQ:
							code_buf[code_idx++] = I386_INS_JZ_Jv;
							break;
						case GENERIC_OPCODE_BGE:
							code_buf[code_idx++] = I386_INS_JNL_Jv;
							break;
						case GENERIC_OPCODE_BGT:
							code_buf[code_idx++] = I386_INS_JNLE_Jv;
							break;
						case GENERIC_OPCODE_BLE:
							code_buf[code_idx++] = I386_INS_JLE_Jv;
							break;
						case GENERIC_OPCODE_BLT:
							code_buf[code_idx++] = I386_INS_JL_Jv;
							break;
						case GENERIC_OPCODE_BNE:
							code_buf[code_idx++] = I386_INS_JNZ_Jv;
							break;
						default:
							assertpro(FALSE);
							break;
					}
				}
				*((int4 *)&code_buf[code_idx]) = jmp_offset;
				code_idx += SIZEOF(int4);
			}
	}
	force_32 = FALSE;
}

void	emit_base_offset(int base_reg, int offset)
{
	CLEAR_EMIT_BASE_INFO;
	emit_base_info.rex = REX_OP; /* All instructions that we generate need to set the REX prefix */
	emit_base_info.flags.modrm_byte_set = TRUE;
	emit_base_info.modrm_byte.modrm.reg_opcode = 0;	/* Not cleared above nor set below */
	/*
	 *	if (offset == 0)
	 *		emit_base_info.modrm_byte.modrm.mod = I386_MOD32_BASE;
	 *	 else
	 */
	if (((-128 <= offset) && (127 >= offset)) && (FALSE == force_32))
		emit_base_info.modrm_byte.modrm.mod = I386_MOD32_BASE_DISP_8;
	else
		emit_base_info.modrm_byte.modrm.mod = I386_MOD32_BASE_DISP_32;
	if ((I386_REG_ESP == (base_reg & 0x7)) || ((I386_REG_EBP == (base_reg & 0x7)) && (0 == offset)))
	{
		emit_base_info.modrm_byte.modrm.r_m = I386_REG_SIB_FOLLOWS;
		/* Refer to the comment in emit_code_sp.h before SET_REX_PREFIX */
		emit_base_info.sib_byte.sib.base = base_reg & 0x7; /* Need only the bottom 3 bits */
		SET_REX_PREFIX(0, REX_B, base_reg);
		emit_base_info.sib_byte.sib.ss = I386_SS_TIMES_1;
		emit_base_info.sib_byte.sib.index = I386_REG_NO_INDEX;
		emit_base_info.flags.sib_byte_set = TRUE;
	} else
	{
		emit_base_info.modrm_byte.modrm.r_m = base_reg & 0x7; /* Need only the bottom 3 bits */
		SET_REX_PREFIX(0, REX_B, base_reg);
	}
	if (((-128 <= offset) && (127 >= offset)) && (FALSE == force_32))
	{
		emit_base_info.offset8 = offset & 0xff;
		emit_base_info.flags.offset8_set = TRUE;
	} else
	{
		emit_base_info.offset32 = offset;
		emit_base_info.flags.offset32_set = TRUE;
	}
}

#ifdef DEBUG

void reset_instruction()
{
	rex_prefix.Base	= 0;
	/* rex_prefix.Index = 0; Not currently used */
	rex_prefix.Reg = 0;
	rex_prefix.Word64 = 0;

	instruction.opcode_mnemonic = NULL;
	instruction.opcode_suffix = 'l';
	instruction.reg_rip = FALSE;
	instruction.reg_prefix = 'e';
	instruction.num_operands = 0;

	instruction.source_operand_class = undefined_class;
	instruction.source_operand_reg = NULL;
	instruction.destination_operand_class = undefined_class;
	instruction.destination_operand_reg = NULL;
	instruction.offset = 0;
	instruction.has_immediate = 0;
	instruction.immediate = 0;
}

/* Now the functions which will print the actual instruction(mnemonics).. */
void print_source_operand()
{
	switch(instruction.source_operand_class)
	{
		case undefined_class :
			assertpro(FALSE);
			break;
		case register_class :
			assert(instruction.source_operand_reg != NULL);
			*obpt++ = '%';
			*obpt++ = instruction.reg_prefix;
			SET_OBPT_STR(instruction.source_operand_reg, STRLEN(instruction.source_operand_reg));
			break;
		case memory_class :
			assert(instruction.destination_operand_class != memory_class);
			if (instruction.source_operand_reg != NULL)
			{
				SET_OBPT_INT4(instruction.offset);
				*obpt++ = '(';
				*obpt++ = '%';
				*obpt++ = instruction.reg_prefix;
				SET_OBPT_STR(instruction.source_operand_reg, STRLEN(instruction.source_operand_reg));
				*obpt++ = ')';
			} else
			{
				SET_OBPT_INT4(instruction.offset);
			}
			break;
		case immediate_class :
			*obpt++ = '0';
			*obpt++ = 'x';
			SET_OBPT_INT8(instruction.immediate);
			break;
		default :
			assertpro(FALSE);
	}
}

void print_destination_operand()
{
	switch(instruction.destination_operand_class)
	{
		case undefined_class :
			assertpro(FALSE);
			break;
		case register_class :
			assert(instruction.destination_operand_reg != NULL);
			*obpt++ = '%';
			*obpt++ = instruction.reg_prefix;
			SET_OBPT_STR(instruction.destination_operand_reg, STRLEN(instruction.destination_operand_reg));
			break;
		case memory_class :
			assert(instruction.source_operand_class != memory_class);
			if (instruction.destination_operand_reg != NULL)
			{
				SET_OBPT_INT4(instruction.offset);
				*obpt++ = '(';
				*obpt++ = '%';
				*obpt++ = instruction.reg_prefix;
				SET_OBPT_STR(instruction.destination_operand_reg, STRLEN(instruction.destination_operand_reg));
				*obpt++ = ')';
			} else
			{
				SET_OBPT_INT4(instruction.offset);
			}
			break;
		case immediate_class :
			*obpt++ = '0';
			*obpt++ = 'x';
			SET_OBPT_INT8(instruction.immediate);
			break;
		default :
			assertpro(FALSE);
	}
}

void print_instruction()
{
	list_chkpage();
	obpt = &outbuf[0];
	memset(obpt, SP, ASM_OUT_BUFF);
	obpt += 10;
	i2hex((curr_addr - PTEXT_OFFSET), obpt, 8);
	curr_addr += (instidx - prev_idx);
	obpt += 10;
	for( ;  prev_idx < instidx; prev_idx++)
	{
		i2hex(code_buf[prev_idx], obpt, 2);
		obpt += 2;
	}
	obpt += 10;
	*obpt++ = '\n';
	*obpt++ = '\t';
	*obpt++ = '\t';
	*obpt++ = '\t';
	*obpt++ = '\t';
	*obpt++ = '\t';
	*obpt++ = '\t';
	assert( instruction.opcode_mnemonic != NULL );
	SET_OBPT_STR(instruction.opcode_mnemonic, STRLEN(instruction.opcode_mnemonic));
	*obpt++ = instruction.opcode_suffix;
	*obpt++ = '\t';
	instruction.num_operands = (instruction.num_operands > grp_prefix)
		? (instruction.num_operands - grp_prefix) : instruction.num_operands;
	switch (instruction.num_operands)
	{
		case 0 :
			break;
		case 1 :
			/* single operand assumed to be in the source operand only.. */
			assert(instruction.destination_operand_class == undefined_class);
			print_source_operand();
			break;
		case 2 :
			print_source_operand();
			*obpt++ = ',';
			print_destination_operand();
			break;
		default :
			assertpro(FALSE);
	}
	/*  Now reset the instruction structure  */
	emit_eoi();
	reset_instruction();
}

void set_memory_reg()
{
	instruction.reg_prefix = 'r';
	if (instruction.source_operand_class == memory_class)
		instruction.source_operand_reg = (char *)register_list[modrm_byte.modrm.r_m + 8 * rex_prefix.Base];
	else if (instruction.destination_operand_class == memory_class)
		instruction.destination_operand_reg = (char *)register_list[modrm_byte.modrm.r_m + 8 * rex_prefix.Base];
	/* Printing of RIP has to be handled differently	*/
 	if (instruction.reg_rip)
		if (instruction.source_operand_class == memory_class)
			instruction.source_operand_reg = (char *)register_list[REG_RIP];
		else if (instruction.destination_operand_class == memory_class)
			instruction.destination_operand_reg = (char *)register_list[REG_RIP];
		else
			assertpro(FALSE);
}

void set_register_reg()
{
	if (instruction.source_operand_class == register_class)
		instruction.source_operand_reg = (char *)register_list[modrm_byte.modrm.reg_opcode + 8 * rex_prefix.Reg];
	else if (instruction.destination_operand_class == register_class)
		instruction.destination_operand_reg = (char *)register_list[modrm_byte.modrm.reg_opcode + 8 * rex_prefix.Reg];
}

void clear_memory_reg()
{
	if (instruction.source_operand_class == memory_class)
		instruction.source_operand_reg = NULL;
	else if (instruction.destination_operand_class == memory_class)
		instruction.destination_operand_reg = NULL;
}

void format_machine_inst()
{
	int		next_inst_byte_meaning = one_byte_opcode;
	int		i, tot_inst_len = 0;
	unsigned char	inst_curr_byte;
	/* None of the flags below are currently used but may be in the near future so are left here */
	boolean_t	lock_prefix_seen;
	boolean_t	rep_e_prefix_seen;
	boolean_t	repne_prefix_seen;
	boolean_t	operand_size_prefix_seen;
	boolean_t	address_size_prefix_seen;

	/* Start Parsing the Instruction Buffer	*/
	instidx = 0;
	prev_idx = 0;
	while(instidx < code_idx)
	{
		switch(next_inst_byte_meaning)
		{	/* Can be a Prefix, or opcode !! */
			case one_byte_opcode :
				inst_curr_byte = code_buf[instidx++];
				instruction.opcode_mnemonic = (char *)mnemonic_list[inst_curr_byte];
				switch(inst_curr_byte)
				{
					/* If Prefixes, set corresponding Flag and continue... 	*/
					case I386_INS_Two_Byte_Escape_Prefix :
						next_inst_byte_meaning = two_byte_opcode;
						break;
					case I386_INS_REX_PREFIX_None :
					case I386_INS_REX_PREFIX__B :
					case I386_INS_REX_PREFIX__X :
					case I386_INS_REX_PREFIX__X_B :
					case I386_INS_REX_PREFIX__R :
					case I386_INS_REX_PREFIX__R_B :
					case I386_INS_REX_PREFIX__R_X :
					case I386_INS_REX_PREFIX__R_X_B :
					case I386_INS_REX_PREFIX__W :
					case I386_INS_REX_PREFIX__W_B :
					case I386_INS_REX_PREFIX__W_X :
					case I386_INS_REX_PREFIX__W_X_B :
					case I386_INS_REX_PREFIX__W_R :
					case I386_INS_REX_PREFIX__W_R_B :
					case I386_INS_REX_PREFIX__W_R_X :
					case I386_INS_REX_PREFIX__W_R_X_B :
						rex_prefix.Base = (inst_curr_byte & 0x01);
						/* rex_prefix.Index = (inst_curr_byte & 0x02) ? 1 : 0; Not currently used */
						rex_prefix.Reg = (inst_curr_byte & 0x04) ? 1 : 0;
						rex_prefix.Word64 = (inst_curr_byte & 0x08) ? 1 : 0;
						if (rex_prefix.Word64)
						{
							instruction.opcode_suffix = 'q';
							instruction.reg_prefix = 'r';
						} else
						{
							instruction.opcode_suffix = 'l';
							instruction.reg_prefix = 'e';
						}
						break;
					case I386_INS_LOCK_Prefix :
						lock_prefix_seen = TRUE;
						break;
					case I386_INS_REPNE_Prefix :
						repne_prefix_seen = TRUE;
						break;
					case I386_INS_REP_E_Prefix :
						rep_e_prefix_seen = TRUE;
						break;
					case I386_INS_Operand_Size_Prefix :
						operand_size_prefix_seen = TRUE;
						break;
					case I386_INS_Address_Size_Prefix :
						address_size_prefix_seen = TRUE;
						break;
					/* Now the instructions having Opcode Extension in the modrm.reg field.. */
					case I386_INS_Grp1_Ev_Iv_Prefix :
					case I386_INS_Grp2_Ev_Iv_Prefix :
						instruction.destination_operand_class = memory_class;
						instruction.source_operand_class = immediate_class;
						instruction.has_immediate = double_word_immediate;
						instruction.num_operands = grp_prefix + 2;
						next_inst_byte_meaning = modrm_sib_bytes;
						break;
					case I386_INS_Grp1_Eb_Ib_Prefix :
					case I386_INS_Grp1_Ev_Ib_Prefix :
					case I386_INS_Grp2_Eb_Ib_Prefix :
						instruction.opcode_suffix = 'b';
						instruction.destination_operand_class = memory_class;
						instruction.source_operand_class = immediate_class;
						instruction.has_immediate = one_byte_immediate;
						instruction.num_operands = grp_prefix + 2;
						next_inst_byte_meaning = modrm_sib_bytes;
						break;
					case I386_INS_Grp2_Eb_1_Prefix :
					case I386_INS_Grp2_Ev_1_Prefix :
					case I386_INS_Grp2_Eb_CL_Prefix :
					case I386_INS_Grp2_Ev_CL_Prefix :
						next_inst_byte_meaning = modrm_sib_bytes;
						assertpro(FALSE);  /* Not taking care of this case for now - not used!! */
						break;
					case I386_INS_Grp3_Eb_Prefix :
						modrm_byte.byte = code_buf[instidx + 1];
						if (modrm_byte.modrm.reg_opcode < 2)
						{
							instruction.destination_operand_class = memory_class;
							instruction.source_operand_class = immediate_class;
							instruction.has_immediate = one_byte_immediate;
							instruction.num_operands = grp_prefix + 2;
						} else
						{
							instruction.source_operand_class = memory_class;
							instruction.opcode_suffix = 'b';
							instruction.num_operands = grp_prefix + 1;
						}
						next_inst_byte_meaning = modrm_sib_bytes;
						break;
					case I386_INS_Grp3_Ev_Prefix :
						modrm_byte.byte = code_buf[instidx + 1];
						if (modrm_byte.modrm.reg_opcode < 2)
						{
							instruction.destination_operand_class = memory_class;
							instruction.source_operand_class = immediate_class;
							instruction.has_immediate = double_word_immediate;
							instruction.num_operands = grp_prefix + 2;
						} else
						{
							instruction.source_operand_class = memory_class;
							instruction.num_operands = grp_prefix + 1;
						}
						next_inst_byte_meaning = modrm_sib_bytes;
						break;
					case I386_INS_Grp4_Prefix :
					case I386_INS_Grp5_Prefix :
						instruction.source_operand_class = memory_class;
						instruction.num_operands = grp_prefix + 1;
						next_inst_byte_meaning = modrm_sib_bytes;
						break;
					/* Now the instructions :  Mainly those who have been used in the code generation in .c
					 *	files.
					 */
					/*	Ins :: OPCODE		*/
					case (I386_INS_PUSH_eAX + I386_REG_RAX) :
					case (I386_INS_PUSH_eAX + I386_REG_RCX) :
					case (I386_INS_PUSH_eAX + I386_REG_RDX) :
					case (I386_INS_PUSH_eAX + I386_REG_RBX) :
					case (I386_INS_PUSH_eAX + I386_REG_RSP) :
					case (I386_INS_PUSH_eAX + I386_REG_RBP) :
					case (I386_INS_PUSH_eAX + I386_REG_RSI) :
					case (I386_INS_PUSH_eAX + I386_REG_RDI) :
						instruction.opcode_suffix = ' ';
						instruction.reg_prefix = 'r';
						instruction.num_operands = 1;
						instruction.source_operand_class = register_class;
						instruction.source_operand_reg = (char *)
							register_list[8 * rex_prefix.Base + inst_curr_byte - I386_INS_PUSH_eAX];
						print_instruction();
						break;
					case (I386_INS_POP_eAX + I386_REG_RAX) :
					case (I386_INS_POP_eAX + I386_REG_RCX) :
					case (I386_INS_POP_eAX + I386_REG_RDX) :
					case (I386_INS_POP_eAX + I386_REG_RBX) :
					case (I386_INS_POP_eAX + I386_REG_RSP) :
					case (I386_INS_POP_eAX + I386_REG_RBP) :
					case (I386_INS_POP_eAX + I386_REG_RSI) :
					case (I386_INS_POP_eAX + I386_REG_RDI) :
						instruction.opcode_suffix = ' ';
						instruction.reg_prefix = 'r';
						instruction.num_operands = 1;
						instruction.source_operand_class = register_class;
						instruction.source_operand_reg = (char *)
							register_list[8 * rex_prefix.Base + inst_curr_byte - I386_INS_POP_eAX];
						print_instruction();
						break;
					case I386_INS_NOP__ :
					case I386_INS_MOVSB_Xb_Yb :
						print_instruction();
						break;
					case I386_INS_MOVSW_D_Xv_Yv:
						assert(rex_prefix.Word64);		/* Expect this to actually be MOVSQ */
						instruction.opcode_mnemonic = "REP MOVS";
						print_instruction();
						break;
					/* Ins :: OPCODE disp8(%rip) 	*/
					case I386_INS_JZ_Jb :
					case I386_INS_JNL_Jb :
					case I386_INS_JNLE_Jb :
					case I386_INS_JLE_Jb :
					case I386_INS_JL_Jb :
					case I386_INS_JMP_Jb :
					case I386_INS_JNZ_Jb :
						instruction.opcode_suffix = ' ';
 						instruction.reg_rip = TRUE;
						instruction.source_operand_class = memory_class;
						set_memory_reg();
						instruction.num_operands = 1;
						next_inst_byte_meaning = one_byte_offset;
						break;
					/* Ins :: OPCODE disp32(%rip) 	*/
					case I386_INS_CALL_Jv :
					case I386_INS_JMP_Jv :
						instruction.opcode_suffix = ' ';
 						instruction.reg_rip = TRUE;
						instruction.source_operand_class = memory_class;
						set_memory_reg();
						instruction.num_operands = 1;
						next_inst_byte_meaning = double_word_offset;
						break;
					/* Ins :: OPCODE IMM8 	*/
					case I386_INS_PUSH_Ib :
						instruction.opcode_suffix = 'b';
						instruction.num_operands = 1;
						instruction.source_operand_class = immediate_class;
						next_inst_byte_meaning = one_byte_immediate;
						break;
					/* Ins :: OPCODE IMM32/64	*/
					case I386_INS_PUSH_Iv :
						instruction.opcode_suffix = 'l';
						instruction.num_operands = 1;
						instruction.source_operand_class = immediate_class;
						if (rex_prefix.Word64 == 0)
							next_inst_byte_meaning = double_word_immediate;
						else
							next_inst_byte_meaning = quad_word_immediate;
						break;
					case I386_INS_CMP_eAX_Iv :
						instruction.num_operands = 2;
						instruction.destination_operand_class = register_class;
						instruction.destination_operand_reg = (char *)register_list[I386_REG_RAX];
						instruction.source_operand_class = immediate_class;
						if (rex_prefix.Word64 == 0)
							next_inst_byte_meaning = double_word_immediate;
						else
							next_inst_byte_meaning = quad_word_immediate;
						break;
					case (I386_INS_MOV_eAX + I386_REG_RAX) :
					case (I386_INS_MOV_eAX + I386_REG_RCX) :
					case (I386_INS_MOV_eAX + I386_REG_RDX) :
					case (I386_INS_MOV_eAX + I386_REG_RBX) :
					case (I386_INS_MOV_eAX + I386_REG_RSP) :
					case (I386_INS_MOV_eAX + I386_REG_RBP) :
					case (I386_INS_MOV_eAX + I386_REG_RSI) :
					case (I386_INS_MOV_eAX + I386_REG_RDI) :
						instruction.num_operands = 2;
						instruction.destination_operand_class = register_class;
						instruction.destination_operand_reg = (char *)
							register_list[8 * rex_prefix.Base + inst_curr_byte - I386_INS_MOV_eAX];
						instruction.source_operand_class = immediate_class;
						if (rex_prefix.Word64 == 0)
							next_inst_byte_meaning = double_word_immediate;
						else
							next_inst_byte_meaning = quad_word_immediate;
						break;
					/* Ins :: OPCODE ModRM (Reg, Mem)/(No_IMM)	*/
					case I386_INS_LEA_Gv_M :
					case I386_INS_MOV_Gv_Ev :
					case I386_INS_CMP_Gv_Ev :
					case I386_INS_XOR_Gv_Ev :
					case I386_INS_MOVSXD_Gv_Ev :
						instruction.num_operands = 2;
						instruction.source_operand_class = memory_class;
						instruction.destination_operand_class = register_class;
						next_inst_byte_meaning = modrm_sib_bytes;
						break;
					/* Ins :: OPCODE ModRM (Mem, Reg)/(No_IMM)	*/
					case I386_INS_MOV_Ev_Gv :
						instruction.num_operands = 2;
						instruction.source_operand_class = register_class;
						instruction.destination_operand_class = memory_class;
						next_inst_byte_meaning = modrm_sib_bytes;
						break;

					/* Ins :: OPCODE ModRM (Mem, IMM) */
					case I386_INS_MOV_Ev_Iv :
						instruction.num_operands = 2;
						instruction.destination_operand_class = memory_class;
						instruction.has_immediate = double_word_immediate;
						instruction.source_operand_class = immediate_class;
						next_inst_byte_meaning = modrm_sib_bytes;
						break;

					default :
						assertpro(FALSE);
				}
				break;
			case two_byte_opcode :
				inst_curr_byte = code_buf[instidx++];
				switch(inst_curr_byte)
				{
					case I386_INS_JO_Jv :
					case I386_INS_JNO_Jv :
					case I386_INS_JB_Jv :
					case I386_INS_JNB_Jv :
					case I386_INS_JZ_Jv :
					case I386_INS_JNZ_Jv :
					case I386_INS_JBE_Jv :
					case I386_INS_JNBE_Jv :
					case I386_INS_JS_Jv :
					case I386_INS_JNS_Jv :
					case I386_INS_JP_Jv :
					case I386_INS_JNP_Jv :
					case I386_INS_JL_Jv :
					case I386_INS_JNL_Jv :
					case I386_INS_JLE_Jv :
					case I386_INS_JNLE_Jv :
 						instruction.reg_rip = TRUE;
						instruction.opcode_mnemonic = (char *) mnemonic_list_2b[inst_curr_byte];
						instruction.source_operand_class = memory_class;
						set_memory_reg();
						instruction.num_operands = 1;
						next_inst_byte_meaning = double_word_offset;
						break;
					default :
						assertpro(FALSE);
				}
				break;
			case modrm_sib_bytes :
				inst_curr_byte = code_buf[instidx++];
				modrm_byte.byte = inst_curr_byte;

				if (instruction.num_operands >= grp_prefix) /* Means reg_opcode = op ext */
				{
					switch((unsigned char) code_buf[instidx - 2])
					{
						case I386_INS_Grp1_Eb_Ib_Prefix :
						case I386_INS_Grp1_Ev_Iv_Prefix :
						case I386_INS_Grp1_Ev_Ib_Prefix :
							instruction.opcode_mnemonic =
								(char *)mnemonic_list_g1[modrm_byte.modrm.reg_opcode];
							break;
						case I386_INS_Grp2_Eb_Ib_Prefix :
						case I386_INS_Grp2_Ev_Iv_Prefix :
						case I386_INS_Grp2_Eb_1_Prefix :
						case I386_INS_Grp2_Ev_1_Prefix :
						case I386_INS_Grp2_Eb_CL_Prefix :
						case I386_INS_Grp2_Ev_CL_Prefix :
							instruction.opcode_mnemonic =
								(char *)mnemonic_list_g2[modrm_byte.modrm.reg_opcode];
							break;
						case I386_INS_Grp3_Eb_Prefix :
						case I386_INS_Grp3_Ev_Prefix :
							instruction.opcode_mnemonic =
								(char *)mnemonic_list_g3[modrm_byte.modrm.reg_opcode];
							break;
						case I386_INS_Grp4_Prefix :
							instruction.opcode_mnemonic =
								(char *)mnemonic_list_g4[modrm_byte.modrm.reg_opcode];
							break;
						case I386_INS_Grp5_Prefix :
							instruction.opcode_suffix = ' ';
							instruction.opcode_mnemonic =
								(char *)mnemonic_list_g5[modrm_byte.modrm.reg_opcode];
							break;
					}
				} else
					set_register_reg();
				set_memory_reg();
				/* Handle the SIB byte ! */
				if ((modrm_byte.modrm.mod != I386_MOD32_REGISTER)
				    && (modrm_byte.modrm.r_m == I386_REG_SIB_FOLLOWS))
				{
					inst_curr_byte = code_buf[instidx++];
					sib_byte.byte = inst_curr_byte;
					/* Assert that the SIB is not used for any complex addressing but is actually a dummy */
					assert((sib_byte.sib.base == I386_REG_ESP) || (sib_byte.sib.base == I386_REG_EBP));
					assert(sib_byte.sib.ss == I386_SS_TIMES_1);
					assert(sib_byte.sib.index == I386_REG_NO_INDEX);

					if (instruction.source_operand_class == memory_class)
						instruction.source_operand_reg =
							(char *)register_list[sib_byte.sib.base + 8 * rex_prefix.Base];
					else if (instruction.destination_operand_class == memory_class)
						instruction.destination_operand_reg =
							(char *)register_list[sib_byte.sib.base + 8 * rex_prefix.Base];
					switch(modrm_byte.modrm.mod)
					{
						case I386_MOD32_BASE :
							if (sib_byte.sib.base == I386_REG_disp32_NO_BASE)
							{
								clear_memory_reg();
								next_inst_byte_meaning = double_word_offset;
							} else if (instruction.has_immediate)
								next_inst_byte_meaning = instruction.has_immediate;
							else
							{
								print_instruction();
								next_inst_byte_meaning = one_byte_opcode;
							}
							break;
						case I386_MOD32_BASE_DISP_8 :
							next_inst_byte_meaning = one_byte_offset;
							break;
						case I386_MOD32_BASE_DISP_32 :
							next_inst_byte_meaning = double_word_offset;
							break;
						default :
							assertpro(FALSE);
					}
				} else 		/* No SIB */
				{
					switch(modrm_byte.modrm.mod)
					{
						case I386_MOD32_BASE :
							if (modrm_byte.modrm.r_m == I386_REG_disp32_FROM_RIP)
							{
								instruction.reg_rip = TRUE;
								set_memory_reg();
								next_inst_byte_meaning = double_word_offset;
							} else
							{
								instruction.offset = 0;
								if (instruction.has_immediate)
									next_inst_byte_meaning = instruction.has_immediate;
								else
								{
									print_instruction();
									next_inst_byte_meaning = one_byte_opcode;
								}
							}
							break;
						case I386_MOD32_BASE_DISP_8 :
							next_inst_byte_meaning = one_byte_offset;
							break;
						case I386_MOD32_BASE_DISP_32 :
							next_inst_byte_meaning = double_word_offset;
							break;
						case I386_MOD32_REGISTER :
							if (instruction.source_operand_class == memory_class)
								instruction.source_operand_class = register_class;
							else if (instruction.destination_operand_class == memory_class)
								instruction.destination_operand_class = register_class;

							if (instruction.has_immediate)
								next_inst_byte_meaning = instruction.has_immediate;
							else
							{
								print_instruction();
								next_inst_byte_meaning = one_byte_opcode;
							}
							break;
						default :
							assertpro(FALSE);
					}
				}
				break;
			case one_byte_immediate :
				instruction.immediate = code_buf[instidx++];
				print_instruction();
				next_inst_byte_meaning = one_byte_opcode;
				break;
			case double_word_immediate :
				instruction.immediate = *((int *)&code_buf[instidx]);
				instidx += 4;
				print_instruction();
				next_inst_byte_meaning = one_byte_opcode;
				break;
			case quad_word_immediate :
				instruction.immediate = *((long *)&code_buf[instidx]);
				instidx += 8;
				print_instruction();
				next_inst_byte_meaning = one_byte_opcode;
				break;
			case one_byte_offset :
				instruction.offset = code_buf[instidx++];
				if (instruction.has_immediate)
					next_inst_byte_meaning = instruction.has_immediate;
				else
				{
					print_instruction();
					next_inst_byte_meaning = one_byte_opcode;
				}
				break;
			case double_word_offset :
				instruction.offset = *((int *)&code_buf[instidx]);
				instidx += 4;
				if (instruction.has_immediate)
					next_inst_byte_meaning = instruction.has_immediate;
				else
				{
					print_instruction();
					next_inst_byte_meaning = one_byte_opcode;
				}
				break;
			case quad_word_offset :
				instruction.offset = *((long *)&code_buf[instidx]);
				instidx += 8;
				if (instruction.has_immediate)
					next_inst_byte_meaning = instruction.has_immediate;
				else
				{
					print_instruction();
					next_inst_byte_meaning = one_byte_opcode;
				}
				break;
			default :
				assertpro(FALSE);
		 }
	}
}
#endif /* DEBUG */
