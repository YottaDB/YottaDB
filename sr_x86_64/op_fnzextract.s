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
	.include "mval_def.si"
#	include "debug.si"

#
# op_fnzextract.s
#
# Mumps $Extract function
#
#	op_fnzextract (int last, int first, mval *src, mval *dest)
#

last		= -12
first		= -16
src		= -24
dest		= -32
FRAME_SIZE	= 32

	.text
	.extern	n2s

ENTRY	op_fnzextract
	pushq	%rbp					# Preserve caller's %rpb register (aka REG_STACK_FRAME)
	movq	%rsp, %rbp				# Save current stack pointer to %rbp
	subq	$FRAME_SIZE, %rsp			# Allocate save area for parameters
	CHKSTKALIGN					# Verify stack alignment
	movl	%edi, last(%rbp)			# Save the parameters
	movl	%esi, first(%rbp)
	movq	%rcx, dest(%rbp)
	mv_force_defined %rdx, l00
	movq	%rdx, src(%rbp)
	mv_force_str %rdx, l01
	movq	src(%rbp), %rsi
	movl	first(%rbp), %eax
	cmpl	$0, %eax
	jg	l10
	movl	$1, %eax				# If first < 1, then first = 1
l10:
	movl	last(%rbp), %edx
	movq	dest(%rbp), %rdi
	movw	$mval_m_str, mval_w_mvtype(%rdi)
	movl	mval_l_strlen(%rsi), %ecx
	cmpl	%eax, %ecx			# If left index > str. len, then null result
	jl	l25
	cmpl	%edx, %ecx			# Right index may be at most the len.
	jge	l20					# .. of the source string
	movl	%ecx, %edx
l20:
	movl	%edx, %r11d
	subl	%eax, %r11d		# Result len. = end - start + 1
	addl	$1, %r11d
	jg	l30					# If len > 0, then continue
l25:
	movl	$0, mval_l_strlen(%rdi)
	jmp	retlab
l30:
	movl 	%r11d, mval_l_strlen(%rdi)
	subl	$1, %eax				# Base = src.addr + left ind. - 1
	addq	mval_a_straddr(%rsi), %rax
	movq	%rax, mval_a_straddr(%rdi)
retlab:
	addq	$FRAME_SIZE, %rsp			# Pull save area back off of stack
	popq	%rbp
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
