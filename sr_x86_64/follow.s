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

	.text
	.extern	op_follow

ENTRY	follow
	subq	$8, %rsp		# Align to 16 bytes
	CHKSTKALIGN			# Verify stack alignment
	movq	%rdi, %rax
	movq	%rsi, %r10
	call	op_follow
	jle	notfollow
	movq	$1, %rax
	jmp	done
notfollow:
	movq	$0, %rax
done:
	addq	$8, %rsp		# Remove stack extension
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
