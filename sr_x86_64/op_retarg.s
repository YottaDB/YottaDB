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
	.extern	unw_retarg

#
# Routine to unwind the M stack returning a value. Note this routine burns the return value like many others
# but waits until the call is complete to do so. This better keeps the debugger trace-back information intact.
# But because we wait, we need an initial bump to the C stack pointer to get it aligned.
#
# Args:
#   %rax - return value mval
#   %r10 - alias value flag
#
ENTRY	op_retarg
	subq	$8, %rsp				# Bump stack for 16 byte alignment
	CHKSTKALIGN					# Verify stack alignment
	movq	%rax, %rdi
	movq	%r10, %rsi
	call	unw_retarg
	addq	$16, %rsp				# Remove stack alignment bump & burn return address
	getframe					# Load regs for prev stack frame & push return addr
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
