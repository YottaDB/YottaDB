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
# mval2bool.s
#	Convert mval to bool
# arg:
#	%r10 - (aka %r10) source mval pointer
# return value:
#	condition code is set
#

	.include "g_msf.si"
	.include "linkage.si"
	.include "mval_def.si"
#	include "debug.si"

	.text
	.extern	s2n
	.extern	underr
	.extern	underr_strict

ENTRY	mval2bool
	subq	$8, %rsp			# Allocate area to align stack to 16 bytes
	CHKSTKALIGN				# Verify stack alignment
	mv_force_defined %r10, isdefined
	movq	%r10, 0(%rsp)		# Save mval addr across potential call (it may have been changed)
	mv_force_num %r10, skip_conv
	movq    0(%rsp), %r10		# Restore mval addr
	addq	$8, %rsp			# Release save area
	cmpl    $0, mval_l_m1(%r10)		# Set condition of flag register
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
