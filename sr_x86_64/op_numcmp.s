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
#
# op_numcmp calls numcmp to compare two mvals
#
# entry:
#   rax	- mval *u
#   rdx	- mval *v
#
# exit:
#   condition codes set according to value of numcmp(u, v)
#

	.text
	.extern	numcmp

ENTRY	op_numcmp
	subq	$8, %rsp			# Bump stack for 16 byte alignment
	CHKSTKALIGN				# Verify stack alignment
	movq	%r10, %rsi
	movq	%rax, %rdi
	call	numcmp
	addq	$8, %rsp			# Remove stack alignment bump
	cmpq	$0, %rax			# Set flags according to result from numcmp
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
