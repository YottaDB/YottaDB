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

	.include "linkage.si"
	.include "g_msf.si"
#	include "debug.si"

	.data
	.extern	frame_pointer

	.text
	.extern	op_unwind

ENTRY	op_iretmvad
	putframe
	addq    $8, %rsp		# Burn return PC and 16 byte align stack
	subq	$16, %rsp		# Bump stack for 16 byte alignment and a save area
	CHKSTKALIGN			# Verify stack alignment
	movq	%r10, 0(%rsp)
	call	op_unwind
	movq	0(%rsp), %rax		# Return input parameter
	addq	$16, %rsp		# Unwind C frame save area
	getframe			# Pick up new stack frame regs & push return addr
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
