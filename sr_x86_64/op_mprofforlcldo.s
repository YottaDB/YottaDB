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
	.extern	exfun_frame_sp

#
# This is the M profiling version which calls different routine(s) for M profiling purposes.
#
ENTRY	op_mprofforlcldow
ENTRY	op_mprofforlcldol
ENTRY	op_mprofforlcldob
	subq	$8, %rsp				# Bump stack for 16 byte alignment
	CHKSTKALIGN					# Verify stack alignment
	movq	frame_pointer(%rip), %rdx
	movq	8(%rsp), %rax			# Get our return address
	addq    %rdi, %rax			# Add in return offset parm
        movq	%rax, msf_mpc_off(%rdx)	# Save as return address for this frame
	call	exfun_frame_sp				# Create new frame
	movq	frame_pointer(%rip), %rbp # Get updated frame pointer
	movq	msf_temps_ptr_off(%rbp), %r14 # .. and updated temps pointer
	addq	$8, %rsp				# Remove our stack alignment bump
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
