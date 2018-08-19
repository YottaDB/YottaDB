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

	.data
	.extern	neterr_pending
	.extern	restart_pc

	.text
	.extern	gvcmz_neterr
	.extern	async_action
	.extern	outofband_clear

ENTRY	op_forintrrpt
	subq	$8, %rsp			# Allocate save area and align stack to 16 bytes
	CHKSTKALIGN				# Verify stack alignment
	cmpb	$0, neterr_pending(%rip)
	je	l1
	call	outofband_clear
	movq	$0, %rdi
	call	gvcmz_neterr
l1:
	movl	$0, %edi
	call	async_action
	addq	$8, %rsp			# Remove alignment stack bump
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
