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
#
#	Called with the stack contents:
#		call return
#		ptr to index mval
#		ptr to step mval
#		ptr to terminator mval
#		loop address
#
	.data
	.extern	frame_pointer
ten_dd:	.long	10

	.text
	.extern	add_mvals
	.extern	numcmp
	.extern	s2n

indx		= -8
step		= -16
term		= -24
loop		= -32
FRAME_SIZE	= 40					# Includes 8 bytes padding to 16 byte align stack

ENTRY	op_forloop
	movq	frame_pointer(%rip), %r11
	popq	msf_mpc_off(%r11)
	pushq	%rbp					# Save %rbp (aka GTM_FRAME_POINTER)
	movq	%rsp, %rbp				# Copy of stack pointer
	subq	$FRAME_SIZE, %rsp			# Create save area that leaves stack 16 byte aligned
	CHKSTKALIGN					# Verify stack alignment
	#
	# Save the arguments on stack
	#
        movq	%rcx, loop(%rbp)
        movq	%rdx, term(%rbp)
        movq	%rsi, step(%rbp)
        movq	%rdi, indx(%rbp)
	movq	%rdi, %rsi
	mv_force_defined_strict %rsi, l0		# Disregard NOUNDEF - errors if not defined
	mv_force_num %rsi, l1
	movq	indx(%rbp), %rsi
	movq	step(%rbp), %rdi
	movw	mval_w_mvtype(%rsi), %ax
	movw	mval_w_mvtype(%rdi), %dx
	andw	%dx, %ax
	testw	$mval_m_int_without_nm, %ax
	je	L66
	movl	mval_l_m1(%rsi), %eax
	addl	mval_l_m1(%rdi), %eax
	cmpl	$MANT_HI, %eax
	jge	L68
	cmpl	$-MANT_HI, %eax
	jle	L67
	movw	$mval_m_int, mval_w_mvtype(%rsi)
	movl	%eax, mval_l_m1(%rsi)
	jmp	L63
L67:
	movb	$mval_esign_mask, mval_b_exp(%rsi) # Set sign bit
	negl	%eax
	jmp	L69
L68:
	movb	$0, mval_b_exp(%rsi)		# Clear sign bit
L69:
	movw	$mval_m_nm, mval_w_mvtype(%rsi)
	orb	$69, mval_b_exp(%rsi)		# Set exponent field
	movl	%eax, %r11d
	movl	$0, %edx
	idivl	ten_dd(%rip), %eax
	movl	%eax, mval_l_m1(%rsi)
	imull	$10, %eax, %eax
	subl	%eax, %r11d
	imull	$MANT_LO, %r11d, %r11d
	movl	%r11d, mval_l_m0(%rsi)
	jmp	L63
L66:
	movq	%rsi, %rcx
	movl	$0, %edx
	movq	%rdi, %rsi
	movq	%rcx, %rdi
	call	add_mvals
	movq	indx(%rbp), %rsi
L63:
	movq	step(%rbp), %rdi
	testw	$mval_m_int_without_nm, mval_w_mvtype(%rdi)
	jne	a
	cmpb	$0, mval_b_exp(%rdi)
	jl	b
	jmp	a2
a:
	cmpl	$0, mval_l_m1(%rdi)
	jl	b
a2:
	movq	term(%rbp), %rdi
	jmp	e
b:
	movq	%rsi, %rdi			# If step is negative, reverse compare
	movq	term(%rbp), %rsi
e:
	#
	# Compare indx and term
	#
	movw	mval_w_mvtype(%rsi), %ax
	movw	mval_w_mvtype(%rdi), %dx
	andw	%dx, %ax
	testw	$2, %ax
	je	ccmp
	movl	mval_l_m1(%rsi), %eax
	subl	mval_l_m1(%rdi), %eax
	jmp	tcmp
ccmp:
	xchgq	%rdi, %rsi
	call	numcmp
	cmpl	$0, %eax
tcmp:
	jle	newiter
	movq	indx(%rbp), %rsi
	movq	step(%rbp), %rdi
	movw	mval_w_mvtype(%rsi), %ax
	movw	mval_w_mvtype(%rdi), %dx
	andw	%dx, %ax
	testw	$mval_m_int_without_nm, %ax
	je	l66
	movl	mval_l_m1(%rsi), %eax
	subl	mval_l_m1(%rdi), %eax
	cmpl	$MANT_HI, %eax
	jge	l68
	cmpl	$-MANT_HI, %eax
	jle	l67
	movw	$mval_m_int, mval_w_mvtype(%rsi)
	movl	%eax, mval_l_m1(%rsi)
	jmp	done
l67:
	movb	$mval_esign_mask, mval_b_exp(%rsi) # Set sign bit
	negl	%eax
	jmp	l69
l68:
	movb	$0, mval_b_exp(%rsi)		# Clear sign bit
l69:
	movw	$mval_m_nm, mval_w_mvtype(%rsi)
	orb	$69, mval_b_exp(%rsi)
	movl	%eax, %r11d
	movl	$0, %edx				# Set edx to 0 before div
	idivl	ten_dd(%rip), %eax
	movl	%eax, mval_l_m1(%rsi)
	imull	$10, %eax, %eax
	subl	%eax, %r11d
	imull	$MANT_LO, %r11d, %r11d
	movl	%r11d, mval_l_m0(%rsi)
	jmp	done
l66:
	movq	%rsi, %rcx
	movl	$1, %edx
	movq	%rdi, %rsi
	movq	%rcx, %rdi			# First and fourth args are same
	call	add_mvals
done:
	movq	%rbp, %rsp				# Unwind save frame
	popq	%rbp					# Restore caller's %rbp
	movq	frame_pointer(%rip), %r11
	pushq	msf_mpc_off(%r11)		# End of loop - return to caller
	ret
newiter:
	#
	# Return to loop return address for another iteration
	#
	movq	loop(%rbp), %r11		# Save loop return address
	movq	%rbp, %rsp				# Unwind save frame
	popq	%rbp					# Restore caller's %rbp
	pushq	%r11				# Set return address
	ret
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
