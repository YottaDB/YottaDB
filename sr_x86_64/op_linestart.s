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
	.extern	frame_pointer

	.text

	#
	# Routine to save the current return address and context in the current stack frame.
	#
	# Since this routine is a leaf routine (no calls), its stack frame alignment is not critical. If that changes,
	# this routine should do the necessary to keep the stack 16 byte aligned and use the CHKSTKALIGN macro to verify
	# it is so.
	#
ENTRY	op_linestart
	movq    frame_pointer(%rip), %r10	# -> M frame
        movq    (%rsp), %rax			# Fetch return address to save
        movq    %rax, msf_mpc_off(%r10)	# Save incoming return address in frame_pointer->mpc
	movq    %r15, msf_ctxt_off(%r10)	# Save ctxt in frame_pointer
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
