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
	.include "error.si"
#	include "debug.si"

	.data
	.extern	dollar_truth
	.extern	xfer_table
	.extern	frame_pointer
	.extern	msp
	.extern	mumps_status
	.extern	restart

	.text
	.extern	mdb_condition_handler
	.extern	op_unwind
	.extern _sigsetjmp			# setjmp() is really __sigsetjmp(env,0)

ENTRY	dm_start
	pushq	%rbp				# Preserve caller's %rbp register (aka REG_STACK_FRAME) which 16 byte aligns stack
	subq	$SUPER_STACK_SIZE, %rsp	# Create super-stack-frame with room for many args
	save_callee_saved
	CHKSTKALIGN				# Verify stack alignment
	movq    %rbp, REG_FRAME_POINTER_SAVE_OFF(%rsp)  # Save the %rbp value, as that will be trashed in the runtime
	movq	%rbx, REG_XFER_TABLE_SAVE_OFF(%rsp)
	movl    $1, mumps_status(%rip)
	leaq	xfer_table(%rip), %rbx
	movl	$1, dollar_truth(%rip)
	ESTABLISH l30
	movq    restart(%rip), %r11
	call    *%r11
return:
	movl	mumps_status(%rip), %eax
	movq	REG_XFER_TABLE_SAVE_OFF(%rsp), %rbx
	movq	REG_FRAME_POINTER_SAVE_OFF(%rsp), %rbp  # Restore the %rbp value, as it will be trashed in runtime
	restore_callee_saved
	addq	$SUPER_STACK_SIZE, %rsp	# Unwind super stack
	popq	%rbp				# Restore caller's %rbp register
	ret

ENTRY	gtm_ret_code
	CHKSTKALIGN				# Verify stack alignment
	REVERT
	call	op_unwind
	movq	msp(%rip), %rax
	movq	(%rax), %rax
	movq	%rax, frame_pointer(%rip)
	addq	$8, msp(%rip)
	jmp	return

ENTRY	gtm_levl_ret_code
	CHKSTKALIGN				# Verify stack alignment
	REVERT
	jmp	return
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
