#################################################################
#								#
# Copyright (c) 2007-2015 Fidelity National Information 	#
# Services, Inc. and/or its subsidiaries. All rights reserved.	#
#								#
# Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.	#
# All rights reserved.						#
#								#
#	This source code contains the intellectual property	#
#	of its copyright holder(s), and is made available	#
#	under a license.  If you do not know the terms of	#
#	the license, please stop and do not read further.	#
#								#
#################################################################

	.include "linkage.si"
	.include "g_msf.si"
#	include "debug.si"

save0	= 0						# Stack offset for first save arg
save1	= 8						# Stack offset for 2nd save arg

	.data
	.extern	frame_pointer
	.extern	zstep_level

	.text
	.extern	gtm_fetch
	.extern	op_retarg
	.extern	op_zbreak
	.extern	op_zst_break
	.extern	op_zst_over
	.extern	op_zstepret
	.extern	opp_ret

ENTRY	opp_zstepret
	subq	$8, %rsp				# Align stack to 16 bytes
	CHKSTKALIGN					# Verify stack alignment
	movq	frame_pointer(%rip), %rax
	movw	msf_typ_off(%rax), %r11w
	testw	$1, %r11w
	je	l1
	movq	zstep_level(%rip), %rdx
	cmpq	%rax, %rdx
	jg	l1
	call	op_zstepret
l1:
	addq	$8, %rsp				# Remove stack alignment bump
	jmp	opp_ret

ENTRY	opp_zstepretarg
	subq	$24, %rsp				# Align stack to 16 bytes plus 2 long int save areas
	CHKSTKALIGN					# Verify stack alignment
	movq	%rax, save0(%rsp)			# Save input regs
	movq	%r10, save1(%rsp)
	call	op_zstepretarg_helper
	movq	frame_pointer(%rip), %rax
	movq	zstep_level(%rip), %rdx
	cmpq	%rax, %rdx
	jg	l2
	call	op_zstepret
l2:
	movq	save1(%rsp), %r10			# Restore input regs
	movq	save0(%rsp), %rax
	addq	$24, %rsp				# Remove our stack bump
	jmp	op_retarg

ENTRY	op_zbfetch
	movq	frame_pointer(%rip), %rax
	popq	msf_mpc_off(%rax)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	movb    $0, %al             			# Variable length argumentt
	call	gtm_fetch
	movq	frame_pointer(%rip), %rdi
	call	op_zbreak
	getframe					# Pushes return addr on stack
	ret

ENTRY	op_zbstart
	movq	frame_pointer(%rip), %rdi
	popq	msf_mpc_off(%rdi)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	call	op_zbreak
	getframe					# Pushes return addr on stack
	ret

ENTRY	op_zstepfetch
	movq	frame_pointer(%rip), %rax
	popq	msf_mpc_off(%rax)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	movb    $0, %al             			# Variable length argument
	call	gtm_fetch
	call	op_zst_break
	getframe					# Pushes return addr on stack
	ret

ENTRY	op_zstepstart
	movq	frame_pointer(%rip), %rdx
	popq	msf_mpc_off(%rdx)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	call	op_zst_break
	getframe					# Pushes return addr on stack
	ret

ENTRY	op_zstzbfetch
	movq	frame_pointer(%rip), %rax
	popq	msf_mpc_off(%rax)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	movb    $0, %al             			# Variable length argument
	call	gtm_fetch
	movq	frame_pointer(%rip), %rdi
	call	op_zbreak
	call	op_zst_break
	getframe
	ret

ENTRY	op_zstzbstart
	movq	frame_pointer(%rip), %rdi
	popq	msf_mpc_off(%rdi)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	call	op_zbreak
	call	op_zst_break
	getframe					# Pushes return addr on stack
	ret

ENTRY	op_zstzb_fet_over
	movq	frame_pointer(%rip), %rax
	popq	msf_mpc_off(%rax)			# Save return address and remove from stack (now 16 byte aligned)
	movb    $0, %al             			# Variable length argument
	CHKSTKALIGN					# Verify stack alignment
	call	gtm_fetch
	movq	frame_pointer(%rip), %rdi
	call	op_zbreak
	movq	zstep_level(%rip), %rdx
	movq    frame_pointer(%rip), %r11
	cmpq	%r11, %rdx
	jae	l3
	cmpl	$0, %eax
	jne	l5
	jmp	l4
l3:
	call	op_zst_break
l4:
	getframe					# Pushes return addr on stack
	ret
l5:
	call	op_zst_over
	movq	frame_pointer(%rip), %rdx
	pushq	msf_mpc_off(%rip)			# Restore return address
	ret

ENTRY	op_zstzb_st_over
	movq	frame_pointer(%rip), %rdi
	popq	msf_mpc_off(%rdi)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	call	op_zbreak
	movq	zstep_level(%rip), %rdi
	movq	frame_pointer(%rip), %r11
	cmpq	%r11, %rdi
	jae	l6
	cmpl	$0, %eax
	jne	l8
	jmp	l7
l6:
	call	op_zst_break
l7:
	getframe					# Pushes return addr on stack
	ret
l8:
	call	op_zst_over
	movq	frame_pointer(%rip), %rdi
	pushq	msf_mpc_off(%rdi)			# Restore return address
	ret

ENTRY	op_zst_fet_over
	movq	frame_pointer(%rip), %rax
	popq	msf_mpc_off(%rax)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	movb    $0, %al					# Variable length argument
	call	gtm_fetch
	movq	zstep_level(%rip), %rax
	movq	frame_pointer(%rip), %r11
	cmpq	%r11, %rax
	jg	l9
	call	op_zst_break
	getframe					# Pushes return addr on stack
	ret
l9:
	call	op_zst_over
	movq	frame_pointer(%rip), %rax
	pushq	msf_mpc_off(%rax)			# Restore return address
	ret

ENTRY	op_zst_st_over
	movq	frame_pointer(%rip), %rax
	popq	msf_mpc_off(%rax)			# Save return address and remove from stack (now 16 byte aligned)
	CHKSTKALIGN					# Verify stack alignment
	movq	zstep_level(%rip), %rdx
	cmpq	%rax, %rdx
	jg	l10
	call	op_zst_break
	getframe					# Pushes return addr on stack
	ret
l10:
	call	op_zst_over
	movq	frame_pointer(%rip), %rdx
	pushq	msf_mpc_off(%rdx)			# Restore return address
	ret

ENTRY	opp_zst_over_ret
	subq	$8, %rsp				# Align stack to 16 bytes
	CHKSTKALIGN					# Verify stack alignment
	movq	frame_pointer(%rip), %rax
	movw	msf_typ_off(%rax), %dx
	testw	$1, %dx
	je	l11
	movq	zstep_level(%rip), %rdx
	movq	msf_old_frame_off(%rax), %rax
	cmpq	%rax, %rdx
	jg	l11
	call	op_zstepret
l11:
	addq	$8, %rsp				# Remove stack alignment bump
	jmp	opp_ret

ENTRY	opp_zst_over_retarg
	subq	$24, %rsp				# Align stack to 16 bytes plus 2 long int save areas
	CHKSTKALIGN					# Verify stack alignment
	movq	%rax, save0(%rsp)			# Save input regs
	movq	%r10, save1(%rsp)
	call	op_zst_over_retarg_helper
	movq	frame_pointer(%rip), %rax
	movq	msf_old_frame_off(%rax), %rax
	movq	zstep_level(%rip), %rdx
	cmpq	%rax, %rdx
	jg	l12
	call	op_zstepret
l12:
	movq	save1(%rsp), %r10			# Restore input regs
	movq	save0(%rsp), %rax
	addq	$24, %rsp				# Remove our stack bump
	jmp	op_retarg
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
