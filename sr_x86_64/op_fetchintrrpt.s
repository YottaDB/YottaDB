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
	.extern	neterr_pending

	.text
	.extern	gtm_fetch
	.extern	gvcmz_neterr
	.extern	outofband_clear
	.extern	async_action

ENTRY	op_fetchintrrpt
	movq	frame_pointer(%rip), %r11
	popq	msf_mpc_off(%r11)		# Save return addr in M frame, also aligns stack to 16 bytes
	CHKSTKALIGN					# Verify stack alignment
	movq    %r15, msf_ctxt_off(%r11)
	movb    $0, %al             		# Variable length argument
	call	gtm_fetch
	cmpb	$0, neterr_pending(%rip)
	je	l1
	call	outofband_clear
	movq 	$0, %rdi
	call	gvcmz_neterr
l1:
	movl	$1, %edi
	call	async_action
	movq	frame_pointer(%rip), %r11
	pushq	msf_mpc_off(%r11)		# Push return address for current frame back on stack
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
