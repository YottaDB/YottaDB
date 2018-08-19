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
# Routine to transfer control from C to M environment driving the top M frame on the stack with optional
# (depending on thevalue of proc_act_typ) compilation of dynamic code (device or error handler or
# an outofband driver).
#
	.include "linkage.si"
	.include "g_msf.si"
#	include "debug.si"

	.data
	.extern	frame_pointer
	.extern	proc_act_type
	.extern xfer_table

	.text
	.extern	trans_code

ENTRY	mum_tstart
	addq	$8, %rsp			# Back up over return address (stack should now be 16 byte aligned)
	CHKSTKALIGN				# Verify stack alignment
	cmpw	$0, proc_act_type(%rip)
	je	notrans
	call	trans_code
notrans:
	getframe				# Pushes return addr on stack
	leaq	xfer_table(%rip), %rbx
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
