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
	.extern	pcurrpos

#
# This is the M profiling version which calls different routine(s) for M profiling purposes.
#
ENTRY	op_mproflinestart
	movq	frame_pointer(%rip), %r10
	movq    (%rsp), %rax			# Save return address
	subq	$8, %rsp				# Bump stack for 16 byte alignment
	CHKSTKALIGN					# Verify stack alignment
	movq	%rax, msf_mpc_off(%r10)	# Store return addr in M frame
	movq    %r15, msf_ctxt_off(%r10)	# Save ctxt into M frame
	call	pcurrpos
	addq	$8, %rsp				# Remove stack alignment bump
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
