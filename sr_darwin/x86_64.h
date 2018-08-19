/****************************************************************
 *								*
 *	Copyright 2007 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* 64-bit register definitions: */
/* GTM register usage */
#if 1
#include "i386.h"

/* Used as part of get and set frame. Should be callee-save registers */
#define GTM_REG_LITERAL_BASE    I386_REG_R12
#define GTM_REG_FRAME_VAR_PTR   I386_REG_R13
#define GTM_REG_FRAME_TMP_PTR   I386_REG_R14
#define GTM_REG_PV		I386_REG_R15

/* Set in mum_tstart.s. Should be a callee-save register */
#define GTM_REG_XFER_TABLE      I386_REG_RBX

#define GTM_REG_FRAME_POINTER   I386_REG_RBP
/* Note that GTM_REG_COND_CODE is not assigned a separate register on x86_64.h
 * The following defination is dummy used in emit_jmp as dummy parameter
 * emit_jmp in x86_64 is not an generic function */
#define GTM_REG_COND_CODE       I386_REG_RAX

#define GTM_REG_R0              I386_REG_RAX
#define GTM_REG_R1              I386_REG_R10

#define GTM_REG_ACCUM           I386_REG_RAX
#define GTM_REG_CODEGEN_TEMP    I386_REG_R11

#define MOVL_RETVAL_REG		GTM_REG_R0
#define MOVL_REG_R1		GTM_REG_R1
#define CLRL_REG		GTM_REG_R0  /* Note this is just a dummy reg. There is no ZEROing reg in x86 */
#define CMPL_TEMP_REG		I386_REG_RSI

#define MOVC3_SRC_REG		GTM_REG_R0
#define MOVC3_TRG_REG		GTM_REG_R1

#define MACHINE_FIRST_ARG_REG	I386_REG_RDI
#define MACHINE_REG_ARGS	6

#define CALLS_TINT_TEMP_REG	GTM_REG_R1
#endif
