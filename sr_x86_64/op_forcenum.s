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

	.text
	.extern	s2n
#
# Routine to force the input source mval to a number if it is not already so.
#
#	%r10 [r10] - source mval
#	%rax [rax] - destination mval
#

save_ret0	= 0
save_ret1	= 8
FRAME_SIZE	= 24					# This frame size gives us a 16 byte aligned stack

ENTRY	op_forcenum
	subq	$FRAME_SIZE, %rsp			# Allocate save area and align stack
	CHKSTKALIGN					# Verify stack alignment
	movq	%rax, save_ret0(%rsp)
	mv_force_defined %r10, l00
	movq	%r10, save_ret1(%rsp)
	mv_force_num %r10, l10
	movq 	save_ret1(%rsp), %r10
	movq	save_ret0(%rsp), %rax
	testw	$mval_m_str, mval_w_mvtype(%r10)
	jz	l20
	testw	$mval_m_num_approx, mval_w_mvtype(%r10)
	jz	l40
l20:
	testw	$mval_m_int_without_nm, mval_w_mvtype(%r10)
	jz	l30
	movw	$mval_m_int, mval_w_mvtype(%rax)
	movl	mval_l_m1(%r10), %edx
	movl	%edx, mval_l_m1(%rax)
	jmp	done

l30:
	movw	$mval_m_nm, mval_w_mvtype(%rax)
	movb	mval_b_exp(%r10), %dl
	movb	%dl, mval_b_exp(%rax)
	#
	# Copy the only numeric part of Mval from [r10] to [rax].
	#
	movl	mval_l_m0(%r10), %edx
	movl	%edx, mval_l_m0(%rax)
	movl	mval_l_m1(%r10), %edx
	movl	%edx, mval_l_m1(%rax)
	jmp	done
l40:
	#
	# Copy the Mval from %r10 [r10] to %rax [rax].
	#
	movq	%rax, %rdi
	movq	%r10, %rsi
	movl	$mval_qword_len, %ecx
	REP
	movsq
done:
	addq	$FRAME_SIZE, %rsp			# Remove save area from C stack
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
