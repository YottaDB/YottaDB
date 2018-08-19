#################################################################
#								#
# Copyright (c) 2007-2016 Fidelity National Information		#
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
	.include "mval_def.si"
#	include "debug.si"

#
# op_fnget.s
#
# Mumps $Get function
#
#
#	r10->%r10 - src. mval
#	rax->%rax - dest. mval
#
# Note there is no stack padding for alignment and no check in this routine because it is a leaf routine
# so never calls anything else. The stack is unaligned by 8 bytes due to the return register but that is
# not an issue unless this routine calls something in the future in which case it needs changes to pad
# the stack for alignment and should then also use the CHKSTKALIGN macro to verify it.
#

	.text

ENTRY	op_fnget
	cmpq	$0, %r10
	je	l5				# Source mval does not exist
	mv_if_notdefined %r10, l5		# Branch if source mval is not defined
	movl	$mval_qword_len, %ecx		# Size of mval
	movq	%r10, %rsi			# Set source
	movq	%rax, %rdi			# Set destination
	REP					# Repeat until count is zero
	movsq
	andw	$~mval_m_aliascont, mval_w_mvtype(%rax)	# Don't propagate alias container flag
	ret

	#
	# Source mval either non-existent or undefined. Set return mval to null string and return it
	#
l5:
	movw	$mval_m_str, mval_w_mvtype(%rax)
	movl	$0, mval_l_strlen(%rax)
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
