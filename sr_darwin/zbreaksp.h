/****************************************************************
 *								*
 *	Copyright 2007, 2009 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

typedef unsigned char	zb_code;
#define ZB_CODE_MASK	0xff
#define	INST_TYPE	zb_code

/* The ZBreak command operates by finding the generated code for the op_linestart or op_linefetch for the source
 * line in question and changing the offset in the transfer table load address instruction from the op_linestart or
 * op_linefetch offset to the appropriate zbreak functionality opcode offset.
 * In some platforms(IA64 and ZOS) since the INSTRUCTION LAYOUT is complex we need following
 * macros for instruction manipulation.
 *      EXTRACT_OFFSET_TO_M_OPCODE
 *      FIX_OFFSET_WITH_ZBREAK_OFFSET
 *      EXTRACT_AND_UPDATE_INST
 * These macros are called only when COMPLEX_INSTRUCTION_UPDATE is defined
 * If COMPLEX_INSTRUCTION_UPDATE is not defined portable code in the caller of these macros
 * is invoked.
 */
#undef COMPLEX_INSTRUCTION_UPDATE
