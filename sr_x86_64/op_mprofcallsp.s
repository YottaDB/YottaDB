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
	.extern	dollar_truth
	.extern	frame_pointer

	.text
	.extern	exfun_frame_push_dummy_frame
	.extern	push_tval

#
# op_mprofcallsp - Used to build a new stack level for argumentless DO (also saves $TEST)
#
# This is the M profiling version which calls different routine(s) for M profiling purposes.
#
# Argument:
#	%rdi - Value from OCNT_REF triple that contains the byte offset from the return address
#		     to return to when the level pops.
#
ENTRY	op_mprofcallspl
ENTRY	op_mprofcallspw
ENTRY	op_mprofcallspb
	movq	(%rsp), %rax			# Save return addr in reg
	subq	$8, %rsp				# Bump stack for 16 byte alignment
	CHKSTKALIGN					# Verify stack alignment
	movq	frame_pointer(%rip), %r11
	movq	%rax, msf_mpc_off(%r11) # Save return addr in M frame
	addq	%rdi, msf_mpc_off(%r11) # Add in return offset
	call	exfun_frame_push_dummy_frame		# Copies stack frame and creates new temps
	movl	dollar_truth(%rip), %edi
	call	push_tval
	movq	frame_pointer(%rip), %rbp
	movq	msf_temps_ptr_off(%rbp), %r14
	addq	$8, %rsp				# Remove stack alignment bump
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
