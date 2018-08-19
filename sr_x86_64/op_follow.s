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

sav_rax	=	-8
sav_rdx	=	-16

	.text
	.extern	memvcmp
	.extern	n2s

ENTRY	op_follow
	pushq	%rbp				# Save %rbp (aka %rbp) - aligns stack to 16 bytes
	movq	%rsp, %rbp
	subq	$16, %rsp			# Get 16 byte save area
	CHKSTKALIGN				# Verify stack alignment
        movq    %r10, sav_rdx(%rbp)
	mv_force_defined %rax, l1
	movq	%rax, sav_rax(%rbp)
	mv_force_str %rax, l2
        movq    sav_rdx(%rbp), %r10
	mv_force_defined %r10, l3
	movq    %r10, sav_rdx(%rbp)
	mv_force_str %r10, l4
	movq	sav_rax(%rbp), %rax
        movq    sav_rdx(%rbp), %r10
	movl	mval_l_strlen(%r10), %ecx
	movq 	mval_a_straddr(%r10), %rdx
	movl    mval_l_strlen(%rax), %esi
	movq	mval_a_straddr(%rax), %rdi
	call	memvcmp
	addq	$16, %rsp
	popq	%rbp
	cmpl	$0, %eax			# Set condition code for use by caller
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
