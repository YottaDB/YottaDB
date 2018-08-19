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
	.include "mval_def.si"
#	include "debug.si"

	.text
	.extern	s2n
	.extern underr

save_ret1	= 8
save_ret0	= 0
FRAME_SIZE	= 24					# This size 16 byte aligns the stack

#
# op_neg ( mval *u, mval *v ) : u = -v
#
#	%r10 - source mval      = &v
#	%rax - destination mval = &u
#
ENTRY	op_neg
	subq	$FRAME_SIZE, %rsp			# Create save area and 16 byte align stack
	CHKSTKALIGN					# Verify stack alignment
	movq	%rax, save_ret0(%rsp)		# Save dest mval addr across potential call
	mv_force_defined %r10, isdefined
	mv_if_number %r10, numer			# Branch if numeric
	movq	%r10, save_ret1(%rsp)		# Save src mval (may not be original if noundef set)
	movq	%r10, %rdi			# Move src mval to parm reg for s2n()
	call	s2n
	movq	save_ret1(%rsp), %r10		# Restore source mval addr
numer:
	movq	save_ret0(%rsp), %rax		# Restore destination mval addr
	mv_if_notint %r10, float			# Branch if not an integer
	movw	$mval_m_int, mval_w_mvtype(%rax)
	movl	mval_l_m1(%r10), %r10d
	negl	%r10d
	movl	%r10d, mval_l_m1(%rax)
	jmp	done
float:
	movw	$mval_m_nm, mval_w_mvtype(%rax)
	movb	mval_b_exp(%r10), %r11b
	xorb	$mval_esign_mask, %r11b		# Flip the sign bit
	movb	%r11b, mval_b_exp(%rax)
	movl	mval_l_m0(%r10), %r11d
	movl	%r11d, mval_l_m0(%rax)
	movl	mval_l_m1(%r10), %r11d
	movl	%r11d, mval_l_m1(%rax)
done:
	addq	$FRAME_SIZE, %rsp			# Remove save area from C stack
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
