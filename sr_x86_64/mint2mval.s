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
#
# mint2mval.s
#	Convert int to mval
# args:
#	%rax   - (aka %rax) - Destination mval pointer
#	%r10d  - (aka %r10d) - Input integer value to convert
#
	.include "g_msf.si"
	.include "linkage.si"
	.include "mval_def.si"
#	include "debug.si"

	.text
	.extern	i2mval

ENTRY	mint2mval
	subq	$8, %rsp		# Align stack to 16 bytes
	CHKSTKALIGN			# Verify stack alignment
	movl	%r10d, %esi
        movq	%rax, %rdi
	call	i2mval
	addq	$8, %rsp
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
