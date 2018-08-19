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

	.data
	.extern	restart_pc
	.extern restart_ctxt
	.extern frame_pointer

	.text
#
# Routine to save the address of the *start* of this call along with its context as the restart point should this
# process encounter a restart situation (return from $ZTRAP or outofband call typically).
#
# Since this is a leaf routine (makes no calls), the stack frame alignment is not important so is not adjusted
# or tested. Should that change, the alignment should be fixed and implement use of the CHKSTKALIGN macro made.
#
ENTRY	op_restartpc
	movq	(%rsp), %rax
	subq	$6, %rax 				# XFER call size is constant
	movq	%rax, restart_pc(%rip)
	movq	frame_pointer(%rip), %rax
	movq	msf_ctxt_off(%rax), %r11
	movq	%r11, restart_ctxt(%rip)
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
