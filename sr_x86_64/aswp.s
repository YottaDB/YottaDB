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

	.text
#
# Note since this routine makes no calls, stack alignment is not critical. If ever a call is added then this
# routine should take care to align the stack to 16 bytes and add a CHKSTKALIGN macro.
#
ENTRY	aswp
	movl	%esi, %eax
	lock
	xchg	(%rdi),%eax		# Return original value
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.

#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
