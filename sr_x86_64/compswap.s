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
	#
	# A(latch longword)	Arg0
	# comparison value	Arg1
	# replacement value	Arg2
	#
	# cmpxchg will compare %eax i.e EAX with 1st arg so copy
	# comparison value to EAX (%eax).
	#
	# Note since this routine makes no calls, stack alignment is not critical. If ever a call is added then this
	# routine should take care to align the stack to 16 bytes and add a CHKSTKALIGN macro.
	#

	.text
ENTRY	compswap
	movl	%esi, %eax
	lock
	cmpxchgl  %edx, 0(%rdi)	# compare-and-swap
	jnz	fail
	movl	$1, %eax			# Return TRUE
	ret
fail:
	xor	%eax, %eax		# Return FALSE
	pause
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
