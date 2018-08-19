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
	.include "mval_def.si"
	.include "g_msf.si"

	.data
	.extern	frame_pointer

	.text

#
# Routine to fill in an mval with the current routine name.
#
# Note since this routine makes no calls, stack alignment is not critical. If ever a call is added then this
# routine should take care to align the stack to 16 bytes and add a CHKSTKALIGN macro.
#
ENTRY	op_currtn
	movw	$mval_m_str, mval_w_mvtype(%r10)
	movq	frame_pointer(%rip), %r11
	movq	msf_rvector_off(%r11), %rax
	movl	mrt_rtn_len(%rax), %r11d
	movl	%r11d, mval_l_strlen(%r10)
	movq	mrt_rtn_addr(%rax), %r11
	movq	%r11, mval_a_straddr(%r10)
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
