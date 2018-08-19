/****************************************************************
 *								*
 * Copyright 2001, 2010 Fidelity Information Services, Inc	*
 *								*
 * Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.*
 * All rights reserved.						*
 *								*
 * Copyright (c) 2017-2018 Stephen L Johnson.			*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef EMIT_CODE_INCLUDED
#define EMIT_CODE_INCLUDED

#include <emit_code_sp.h>

#ifdef DEBUG
void	emit_asmlist(triple *ct);
void	emit_eoi(void);
#endif
void    trip_gen(triple *ct);
short	*emit_vax_inst(short *inst, oprtype **fst_opr, oprtype **lst_opr);
void	emit_jmp(uint4 branchop, short **instp, int reg);
void	emit_pcrel(void);
void	emit_trip(oprtype *opr, boolean_t val_output, uint4 alpha_inst, int ra_reg);
void	emit_push(int reg);
void	emit_pop(int count);
void	add_to_vax_push_list(int pushes_seen);
int	next_vax_push_list(void);
void	push_list_init(void);
void    reset_push_list_ptr(void);
void	emit_call_xfer(int xfer);

int	get_arg_reg(void);
int	gtm_reg(int vax_reg);

#ifdef __x86_64__
#  define NUM_BUFFERRED_INSTRUCTIONS 100
#  define CODE_TYPE unsigned char
#elif defined(__ia64)
#  define CODE_TYPE ia64_bundle
#  define NUM_BUFFERRED_INSTRUCTIONS 25
#elif defined(__MVS__) || defined(Linux390)
#  define CODE_TYPE uint2
#  define NUM_BUFFERRED_INSTRUCTIONS 100
#elif defined(__armv6l__)
#  define CODE_TYPE uint4
#  define NUM_BUFFERRED_INSTRUCTIONS 100
#elif defined(__armv7l__)
#  define CODE_TYPE uint4
#  define NUM_BUFFERRED_INSTRUCTIONS 25
#else
#  define CODE_TYPE uint4
#  define NUM_BUFFERRED_INSTRUCTIONS 25
#endif

#define ASM_OUT_BUFF 	256
#define PUSH_LIST_SIZE	500

#if defined(__vms) || defined(_AIX) || defined(__sparc) || defined(__hpux) || (defined(__linux__) && defined(__ia64))	\
	|| defined(__MVS__)
#  define TRUTH_IN_REG
#elif defined(__osf__) || (defined(__linux__) && defined(__x86_64__)) || defined(Linux390) || defined(__APPLE__)	\
	|| (defined(__CYGWIN__) && defined(__x86_64__))									\
	|| (defined(__linux__) && (defined(__armv6l__) || defined(__armv7l__)))
#  undef TRUTH_IN_REG
#else
#  error UNSUPPORTED PLATFORM
#endif

#endif
