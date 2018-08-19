#################################################################
#								#
# Copyright (c) 2007-2015 Fidelity National Information 	#
# Services, Inc. and/or its subsidiaries. All rights reserved.	#
#								#
# Copyright (c) 2017 YottaDB LLC. and/or its subsidiaries.	#
# All rights reserved.						#
#								#
#	This source code contains the intellectual property	#
#	of its copyright holder(s), and is made available	#
#	under a license.  If you do not know the terms of	#
#	the license, please stop and do not read further.	#
#								#
#################################################################

	.include "g_msf.si"
	.include "linkage.si"
#	include "debug.si"
#	include "ydbmerrors.h"

#
# Routine to set up the stack frame for a local (same routine) invocation. It can
# be one of any of the following forms:
#
#   1. A local call with parameters (OC_EXCAL triple - DO rtn(parms)). In this case the
#      address of the return value is NULL.
#   2. A local extrinsic with no parameters (OC_EXFUN triple $$func).
#   2. A local extrinsic with parameters (OC_EXFUN triple $$func(parms)).
#
# Arguments:
#
#   REG_ARG0: Address of return value
#   REG_ARG1: Offset from our return address to where this stackframe needs to return
#   REG_ARG2: Mask
#   REG_ARG3: Actualcnt
#   REG_ARG4: Actuallist1 address
#   REG_ARG5: Actuallist2 address
#   (remainder of args on stack if any)
#
# This is the M profiling version which calls different routine(s) for M profiling purposes.
#
# Note no need to save %rbp in the prologue as it gets reset to the new frame when we return
#

	.data
	.extern	dollar_truth
	.extern	frame_pointer

	.text
	.extern	exfun_frame_sp
	.extern	push_parm
	.extern	rts_error

arg2_off	= -48
arg1_off	= -40
arg0_off	= -32
act_cnt		= -24
mask_arg	= -20
ret_val		= -16
rtn_pc		= -8
FRAME_SAVE_SIZE	= 56					# This size 16 byte aligns the stack

ENTRY	op_mprofexfun
	movq	0(%rsp), %r11				# Save return address for storage in M stack frame
	movq	%rsp, %rbp				# Copy previous stack-frame to %rbp
	subq	$FRAME_SAVE_SIZE, %rsp			# Create save area
	CHKSTKALIGN					# Verify stack alignment
	movq	%r11, rtn_pc(%rbp) 			# Save return address
	movq	%rdi, ret_val(%rbp)			# Save incoming arguments
	movl	%edx, mask_arg(%rbp)
	movl	%ecx, act_cnt(%rbp)
	movq	%r8, arg0_off(%rbp)
	movq	%r9, arg1_off(%rbp)
	movq	frame_pointer(%rip), %rdx
	movq	rtn_pc(%rbp), %rax			# Verify the immediate instruction after
	cmpb	$JMP_Jv, 0(%rax)			# .. this function call
	jne	error
	movq	%rax, msf_mpc_off(%rdx)
	addq	%rsi, msf_mpc_off(%rdx)
	call	exfun_frame_sp
	movl	act_cnt(%rbp), %eax
	cmpl	$0, %eax                         	# arg0, arg1, arg2 are stored in save area off of %rbp
	je	no_arg
	cmpl	$1, %eax				# We have only one register free for push_parm args
	je	arg0
	#
	# We have more than 1 actuallist parameters so we need some aligned space on the stack for parameters that
	# don't fit in the 6 parm registers. Only the first actuallist parameter can fit in a parm register. All
	# others must reside on the stack starting at the lowest address. So for example, if we need 7 slots, we
	# must allocate 8 slots to keep the stack aligned but the 7 slots used must be those with the lowest address
	# for this to work correctly.
	#
	# Normally we would subtract 1 to get the count of parms to be on the stack then round that to a multiple of
	# 2 since 2 parms would be 16 bytes. The rounding would add 1 and shift but to avoid -1 then +1, just do the
	# rounding "AND" on the value we have. Use REG64_ARG/ARG5 as temporary work registers.
	#
	cltq						# Convert %eax to %rax
	movq	%rax, %r9				# Copy of parmcnt
	andq	$-2, %r9				# Round to even value
	shlq	$3, %r9					# Mult by 8 via shifting gives us 16 byte aligned value
	subq	%r9, %rsp				# Allocate aligned parm area on stack
	CHKSTKALIGN					# Verify stack alignment
	movq	%rsp, %r8				# Save bottom of allocation to %r8
	subq	$2, %rax				# Remove one parm to be passed in parmreg and one for 0 origin
	leaq	(%r8, %rax, 8), %r8 			# Address for last actuallist parm to be stored
	cmpq	$0, %rax				# Only 1 arg left?
	je	arg1
	leaq	(%rbp, %rax, 8), %r11			# Address of last passed-in parameter
again:
	movq	0(%r11), %r9				# Move parm to temp register
	movq	%r9, 0(%r8)				# Move parm to home in stack location
	subq	$8, %r11				# Move pointers to previous argument
	subq	$8, %r8
	subq	$1, %rax				# Count down the parm
	cmpq	$0, %rax				# See if down to 1 parm and if so, fall thru to handle it
	jg	again
arg1:
	movq	arg1_off(%rbp), %r11			# Copy parm to register
	movq	%r11, 0(%r8)				# Copy parm to stack resident location
arg0:
	movq	arg0_off(%rbp), %r9  			# Only one argument which can be fitted into REG5
no_arg:
	movl	act_cnt(%rbp), %r8d			# Actual Arg cnt
	movl	mask_arg(%rbp), %ecx			# Mask
	movq	ret_val(%rbp), %rdx			# ret_value
	movl	dollar_truth(%rip), %esi		# $TEST
	andl	$1, %esi
	movl	act_cnt(%rbp), %edi
	addl	$4, %edi				# Totalcount = Act count +4
	movb	$0, %al					# variable length argument
	call	push_parm				# push_parm(total, $T, ret_value, mask, argc [,arg1, arg2, ...])
done:
	#
	# Ready to return - need to pick up new frame pointer value (%rbp) and new allocated temp area
	#
	movq    frame_pointer(%rip), %r11
	movq	msf_temps_ptr_off(%r11), %r14
	movq	%rbp, %rsp				# Unwind C stack back to caller
	movq    %r11, %rbp				# Resets %rbp aka %rbp with frame just created
	ret

error:
	movl	ERR_GTMCHECK(%rip), %esi
	movl	$1, %edi
	movb	$0, %al             			# Variable length argument
	call	rts_error
	jmp	done					# Shouldn't return but in case..
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
