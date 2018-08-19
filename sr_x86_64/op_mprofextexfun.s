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
	.include "g_msf.si"
	.include "gtm_threadgbl_deftypes_asm.si"
#	include "debug.si"
#	include "ydbmerrors.h"

#
# op_mprofextexfun calls an external GT.M MUMPS routine with arguments and provides for
# a return value in most instances. If the routine has not yet been linked into the
# current image, op_mprofextexfun will first link it by invoking the auto-ZLINK function.
# Before driving the function, we check if *any* routines have been linked in as
# autorelink-enabled or if any directories are autorelink-enabled and if so, drive
# a check to see if a newer version exists that could be linked in.
#
# Parameters:
#
#  - arg0 (%rdi) - Index into linkage table of caller containing the routine header
#	           address of the routine to call (rtnidx).
#  - arg1 (%rsi) - Index into linkage table of caller containing the address of an
#		   offset into routine to which to transfer control associated with
#	           a given label. This value is typically the address of the lnr_adr
#		   field in a label entry (lblidx).
#  - arg2 (%rdx) - Address of where return value is placed or NULL if none (ret_value).
#  - arg3 (%rcx) - Bit mask with 1 bit per argument (ordered low to high). When bit is
#		   set, argument is pass-by-value, else pass-by-reference (mask).
#  - arg4 (%r8)  - Count of M routine parameters supplied (actualcnt).
#  - arg5 (%r9)  - List of addresses of mval parameters (actual1).
#
# Note if lblidx (arg1) is negative, this means the linkage table to use is not from the
# caller but is contained in TREF(lnk_proxy) used by indirects and other dynamic
# code (like callins).
#
# Note the return address is also supplied (on the stack) but we remove that immediately
# since we do not return directly to it but to the the called rtn when the return address
# is loaded out of the top M stackframe by getframe.
#
# Note we use %r12 as a flag that we don't do more than one of auto_zlink() OR auto_relink_check()
# call. This keeps us out of any possible loop condition as only one or the other should
# ever be necessary. Register %r12 is also known as %r12 and is saved by the putframe
# macro so we need not save it separately.
#
# Note op_mprofextexfun is a near exact copy of op_extexfun differing only in the name and
# the version of new_stack_frame() it calls (calls new_stack_frame_sp() instead).
#
	.data
	.extern	ERR_FMLLSTMISSING
	.extern	ERR_GTMCHECK
	.extern	dollar_truth
	.extern	frame_pointer
	.extern gtm_threadgbl

	.text
	.extern	auto_zlink
	.extern auto_relink_check
	.extern	new_stack_frame_sp
	.extern	push_parm
	.extern	rts_error
	.extern laberror

arg0_off	= -40
act_cnt		= -32
mask_arg	= -28
ret_val		= -24
lblidx		= -16
rtnidx		= -8
SAVE_SIZE	= 48						# This size 16 byte aligns the stack

ENTRY	op_mprofextexfun
	putframe						# Save registers into current M stack frame
	addq    $8, %rsp            				# Burn return PC (also 16 byte aligns the stack)
	CHKSTKALIGN						# Verify stack alignment
	movq	$0, %r12					# We haven't done auto_zlink/auto_relink_check
	movq	%rsp, %rbp					# Copy stack pointer to %rbp
	subq	$SAVE_SIZE, %rsp				# Allocate save area (16 byte aligned)
	#
	# Note from here down, do *not* use REG_FRAME_POINTER which was overwritten above. REG_FRAME_POINTER  is an alias for
	# register %rbp which contains a copy of %rsp before %rsp was decremented by the save area size so %rbp contains a
	# pointer just past the save area we've allocated which is why all references are using negative offsets.
	#
	movslq	%esi, %rsi				# Sign extend arg1 so can check for negative arg
        movq    %rdi, rtnidx(%rbp)			# Save argument registers
        movq    %rsi, lblidx(%rbp)
        movq    %rdx, ret_val(%rbp)
        movl    %ecx, mask_arg(%rbp)
	movl	%r8d, act_cnt(%rbp)
	movq    %r9, arg0_off(%rbp)
	#
	# First up, check the label index to see if tiz negative. If so, we must use lnk_proxy as a base address
	# and pseudo linkagetable. Else use the caller's linkage table.
	#
	cmpq	$0, %rsi					# Use current frame linkage table or lnk_proxy?
	jge	loadandgo
	#
	# We have a negative index. Use lnk_proxy as a proxy linkage table.
	#
	movq	gtm_threadgbl(%rip), %r10		# %r10 contains threadgbl base
	leaq	ggo_lnk_proxy(%r10), %rax		# -> &lnk_proxy.rtnhdr_adr
	cmpq	$0, %rdi					# Using proxy table, rtnhdr index must be 0
	jne	gtmcheck
	movq	(%rax), %rdi			# -> rtnhdr
	cmpq	$0, %rdi					# See if defined yet
	je	gtmcheck					# If rhdaddr == 0, not yet linked into image which
								# .. should never happen for indirects
	cmpq	$-1, %rsi					# Using proxy table, label index must be -1
	jne	gtmcheck
	leaq	8(%rax), %rsi			# -> label table code offset ptr
	cmpq    $0, 0(%rsi)
        je      gtmcheck					# If labaddr == 0 && rhdaddr != 0, label does not exist
								# .. which also should never happen for indirects
	cmpl	$0, 16(%rax)				# See if a parameter list was supplied
	je	fmllstmissing					# If not, raise error
	jmp	justgo						# Bypass autorelink check for indirects (done by caller)
	#
	# We have a non-negative index. Use args as indexes into caller's linkage table. Note we cannot overwrite
	# REG_FRAME_POINTER as it is being used as %rbp in this routine (copy of %rsp before allocating save area above).
	#
loadandgo:
	movq	frame_pointer(%rip), %rax			# -> frame_pointer
	movq	msf_rvector_off(%rax), %rax		# -> frame_pointer->rvector (rtnhdr)
	movq	mrt_lnk_ptr(%rax), %rax		# -> frame_pointer->rvector->linkage_adr
	shlq	$3, %rdi					# arg * 8 = offset for rtnhdr ptr
	cmpq	$0, (%rax, %rdi)			# See if defined
	je	autozlink					# No - try auto-zlink
	movq	(%rax, %rdi), %rdi		# -> rtnhdr
	#
	# Have rtnhdr to call now. If rtnhdr->zhist, we should do an autorelink check on this routine to see if it needs
	# to be relinked. Only do this if %r12 is 0 meaning we haven't already done an autorelink check or if we just
	# loaded the routine via auto_zlink.
	#
	cmpq	$0, %r12					# Already checked/resolved?
	jne	getlabeloff					# Yes, bypass this check and resolve the label offset
	cmpq	$0, mrt_zhist(%rdi)			# See if we need to do an autorelink check
	jne	autorelink_check				# Need autorelink check
getlabeloff:
	shlq	$3, %rsi					# arg * 8 = offset for label offset pointer
	cmpq	$0, (%rax, %rsi)			# See if defined
	je	label_missing
	movq	(%rax, %rsi), %rsi		# -> label table code offset
	cmpl	$0, 8(%rsi)				# If has_parms == 0, then issue an error
	je	fmllstmissing
	#
	# Create stack frame and invoke routine
	#
justgo:
	movq	0(%rsi), %rax			# &(code_offset) for this label (usually & of lntabent)
	cmpq	$0, %rax
	je	label_missing
	movslq	0(%rax), %rdx			# Code offset for this label
	addq	mrt_ptext_adr(%rdi), %rdx		# Transfer address: codebase reg + offset to label
	movq	mrt_lnk_ptr(%rdi), %rsi		# Linkage table address (context pointer)
	call	new_stack_frame_sp
	#
	# Move parameters into place
	#
	movl	act_cnt(%rbp), %eax			# Number of actuallist parms
	cmpl	$0, %eax
	je	no_arg						# There are no actuallist parms
	cmpl	$1, %eax					# See if just one arg left
        je     	one_arg						# Just one - load it from the saved parm regs
	#
	# We need one or more actuallist parms to reside on the stack as we have overflowed the 6 parameter registers. We
	# need to allocate a 16 byte aligned chunk of stack memory to house those parms. If some of that block is actually
	# padding, the parms need to live in the lower address slots to work correctly with C varargs. So for example, if
	# we need 7 slots, we have to allocate 8 slots and use the lowest addressed 7 slots for vargs to work correctly.
	#
	# Normally we would subtract 1 to get the count of parms to be on the stack then round that to a multiple of
	# 2 since 2 parms would be 16 bytes. The rounding would add 1 and shift but to avoid -1 then +1, just do the
	# rounding "AND" on the value we have. Use REG64_ARG/ARG5 as temporary work registers.
	#
        cltq							# Convert %eax to %rax
	movq	%rax, %r9				# Copy of parmcnt
	andq	$-2, %r9					# Round to even value
	shlq	$3, %r9					# Mult by 8 via shifting gives us 16 byte aligned value
	subq	%r9, %rsp				# Allocate aligned parm area on stack
	CHKSTKALIGN						# Verify stack alignment
	movq	%rsp, %r8				# Save bottom of allocation to %r8
	subq	$2, %rax					# Remove one parm to be passed in parmreg and one for 0 origin
	leaq	(%r8, %rax, 8), %r8	# Address for last actuallist parm to be stored
	leaq	(%rbp, %rax, 8), %r11		# Address of last passed-in parameter
again:
	movq	0(%r11), %r9			# Move parm to temp register
	movq	%r9, 0(%r8)			# Move parm to home in stack location
	subq	$8, %r11				# Move pointers to previous argument
	subq	$8, %r8
	subq	$1, %rax					# Count down the parm
	cmpq	$0, %rax					# Branch unless all parms are done (0 > %rax)
	jnl	again
one_arg:
	movq    arg0_off(%rbp), %r9
no_arg:
	movl	act_cnt(%rbp), %r8d
	movl    mask_arg(%rbp), %ecx
        movq    ret_val(%rbp), %rdx
	movl   	dollar_truth(%rip), %esi
	andl   	$1, %esi
	movl   	act_cnt(%rbp), %edi
	addl   	$4, %edi					# Includes: $TEST, ret_value, mask, act_cnt
	movb    $0, %al					# Variable length argument
	call	push_parm					# push_parm (total, $T, ret_value, mask, argc [,arg1, arg2, ...]);
retlab:
	movq	%rbp, %rsp					# Unwind C stack back to caller
	getframe						# Load regs from top M frame (pushes return reg)
	ret

#
# Drive auto_zlink to fetch module
#
autozlink:
	cmpq	$0, %r12					# Already did autorelink or autorelink check?
	jne	gtmcheck
	movq	rtnidx(%rbp), %rdi			# Get index arg back
	call	auto_zlink
	movq	rtnidx(%rbp), %rdi			# Restore both args after call
	movq	lblidx(%rbp), %rsi
	movq	$1, %r12
	jmp	loadandgo

#
# Drive auto_relink_check to see if a newer routine should be loaded
#
autorelink_check:
	cmpq	$0, %r12					# Already did autorelink or autorelink check?
	jne	gtmcheck
	movq	rtnidx(%rbp), %rdi			# Restore both args as parms for call
	movq	lblidx(%rbp), %rsi
	call	auto_relink_check				# %rdi still populated by rtnhdr
	movq	rtnidx(%rbp), %rdi			# Restore both args after call
	movq	lblidx(%rbp), %rsi
	movq	$2, %r12
	jmp	loadandgo

#
# Raise GTMCHECK (pseudo-GTMASSERT since args are more difficult in assembler) when something really screwedup
# occurs
#
gtmcheck:
	movl	ERR_GTMCHECK(%rip), %esi
	movl	$1, %edi
	movb    $0, %al					# Variable length argument
	call	rts_error
	jmp	retlab

#
# Make call so we can raise the appropriate LABELMISSING error for the not-found label.
#
label_missing:
	movq	lblidx(%rbp), %rdi			# Index to linkage table and to linkage name table
	call	laberror
	jmp	retlab

#
# Raise missing formal list error
#
fmllstmissing:
	movl    ERR_FMLLSTMISSING(%rip), %esi
        movl    $1, %edi
	movb	$0, %al					# Variable length argument
	call	rts_error
	jmp	retlab
#
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
