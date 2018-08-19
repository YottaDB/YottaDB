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
# op_extcall calls an external GT.M MUMPS routine with no arguments. If the routine
# has not yet been linked into the current image, op_extcall will first link it by
# invoking the auto-ZLINK function. Before driving the function, we check if *any*
# routines have been linked in as autorelink-enabled or if any directories are
# autorelink-enabled and if so, drive a check to see if a newer version exists that
# could be linked in.
#
# Parameters:
#
#  - arg0 (%rdi) - Index into linkage table of caller containing the routine header
#	           address of the routine to call.
#  - arg1 (%rsi) - Index into linkage table of caller containing the address of an
#		   offset into routine to which to transfer control associated with
#	           a given label. This value is typically the address of the lnr_adr
#		   field in a label entry
#
# Note if arg1 is negative, this means the linkage table to use is not from the
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
	.data
	.extern	_ERR_GTMCHECK
	.extern	frame_pointer
	.extern gtm_threadgbl

	.text
	.extern	auto_zlink
	.extern auto_relink_check
	.extern	new_stack_frame
	.extern	rts_error
	.extern laberror
#
# Define offsets for arguments pushed back on the stack
#
stack_arg0	= 0
stack_arg1	= 8
SAVE_SIZE	= 16

ENTRY	op_extcall
	putframe						# Save registers into current M stack frame (includes %r12)
	addq    $8, %rsp     					# Burn the saved return pc (also aligns stack to 16 bytes)
	CHKSTKALIGN						# Verify stack alignment
	subq	$SAVE_SIZE, %rsp				# Allocate save area on stack (16 byte aligned)
	movq	$0, %r12					# Init flag - We haven't done auto_zlink/auto_relink_check
	movslq	%esi, %rsi					# Sign extend arg1
	movq	%rsi, stack_arg1(%rsp)				# Save index args
	movq	%rdi, stack_arg0(%rsp)
	#
	# First up, check the label index to see if tiz negative. If so, we must use lnk_proxy as a base address
	# and pseudo linkagetable. Else use the caller's linkage table.
	#
	cmpq	$0, %rsi					# Use current frame linkage table or lnk_proxy?
	jge	loadandgo
	#
	# We have a negative index. Use lnk_proxy as a proxy linkage table.
	#
	movq	gtm_threadgbl(%rip), %r10			# %r10 contains threadgbl base
	leaq	ggo_lnk_proxy(%r10), %rax			# -> lnk_proxy.rtnhdr_adr
	cmpq	$0, %rdi					# Using proxy table, rtnhdr index must be 0
	jne	gtmcheck
	movq	(%rax), %rdi					# -> rtnhdr
	cmpq	$0, %rdi					# See if defined yet
	je	gtmcheck					# If rhdaddr == 0, not yet linked into image which
								# .. should never happen for indirects
	cmpq	$-1, %rsi					# Using proxy table, label index must be -1
	jne	gtmcheck
	leaq	8(%rax), %rsi					# -> label table code offset ptr
	cmpq    $0, 0(%rsi)
        je      gtmcheck					# If labaddr == 0 && rhdaddr != 0, label does not exist
								# .. which also should never happen for indirects
	jmp	justgo						# Bypass autorelink check for indirects (done by caller)
	#
	# We have a non-negative index. Use args as indexes into caller's linkage table.
	#
loadandgo:
	movq	msf_rvector_off(%rbp), %rax			# -> frame_pointer->rvector (rtnhdr)
	movq	mrt_lnk_ptr(%rax), %rax				# -> frame_pointer->rvector->linkage_adr
	shlq	$3, %rdi					# arg * 8 = offset for rtnhdr ptr
	cmpq	$0, (%rax, %rdi)				# See if defined
	je	autozlink					# No - try auto-zlink
	movq	(%rax, %rdi), %rdi				# -> rtnhdr
	#
	# Have rtnhdr to call now. If rtnhdr->zhist, we should do an autorelink check on this routine to see if it needs
	# to be relinked. Only do this if %r12 is 0 meaning we haven't already done an autorelink check or if we just
	# loaded the routine via auto_zlink.
	#
	cmpq	$0, %r12					# Already checked/resolved?
	jne	getlabeloff					# Yes, bypass this check and resolve the label offset
	cmpq	$0, mrt_zhist(%rdi)				# See if we need to do an autorelink check
	jne	autorelink_check				# Need autorelink check
getlabeloff:
	shlq	$3, %rsi					# arg * 8 = offset for label offset ptr
	cmpq	$0, (%rax, %rsi)				# See if defined
	je	label_missing
	movq	(%rax, %rsi), %rsi				# -> label table code offset
	#
	# Create stack frame and invoke routine
	#
justgo:
	movq	0(%rsi), %rax					# &(code_offset) for this label (usually & of lntabent)
	cmpq	$0, %rax
	je	label_missing
	movslq	0(%rax), %rdx					# Code offset for this label
	addq	mrt_ptext_adr(%rdi), %rdx			# Transfer address: codebase reg + offset to label
	movq	mrt_lnk_ptr(%rdi), %rsi				# Linkage table address (context pointer)
	call	new_stack_frame
retlab:								# If error, return to caller, else "return" to callee
	addq	$SAVE_SIZE, %rsp				# Undo save area bump
	getframe						# Sets regs (including %r12) as they should be for new frame
	ret

#
# Drive auto_zlink to fetch module
#
autozlink:
	cmpq	$0, %r12					# Already did autorelink or autorelink check?
	jne	gtmcheck
	movq	stack_arg0(%rsp), %rdi				# Get index arg back
	call	auto_zlink
	movq	stack_arg0(%rsp), %rdi				# Restore both args after call
	movq	stack_arg1(%rsp), %rsi
	movq	$1, %r12
	jmp	loadandgo

#
# Drive auto_relink_check to see if a newer routine should be loaded
#
autorelink_check:
	cmpq	$0, %r12					# Already did autorelink or autorelink check?
	jne	gtmcheck
	movq	stack_arg0(%rsp), %rdi				# Restore both args as parms for call
	movq	stack_arg1(%rsp), %rsi
	call	auto_relink_check				# %rdi still populated by rtnhdr
	movq	stack_arg0(%rsp), %rdi				# Restore both args after call
	movq	stack_arg1(%rsp), %rsi
	movq	$2, %r12
	jmp	loadandgo

#
# Raise GTMCHECK (pseudo-GTMASSERT since args are more difficult in assembler) when something really screwedup
# occurs
#
gtmcheck:
	movl	ERR_GTMCHECK(%rip), %esi
	movl	$1, %edi
	movb    $0, %al						# Variable length argument
	call	rts_error
	jmp	retlab

#
# Make call so we can raise the appropriate LABELMISSING error _for the not-found label.
#
label_missing:
	movq	stack_arg1(%rsp), %rdi			# Index to linkage table and to linkage name table
	call	laberror
	jmp	retlab
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
