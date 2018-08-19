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

	.data
	.extern	undef_inhibit

	.text
	.extern	underr

#
# Routine to compare input mval (passed in %rax) to see if it is the NULL string. Primary return is setting
# the condition code for a conditional branch in the caller. If caller wants/needs it, the return (int) value is
# also set.
#
ENTRY	op_equnul
	subq	$8, %rsp				# Bump stack for 16 byte alignment
	CHKSTKALIGN					# Verify stack alignment
	mv_if_notdefined %rax, undefmval
	testw	$mval_m_str, mval_w_mvtype(%rax)	# See if a string
	je	notnullstr				# If not a string, then not a null string
	cmpl	$0, mval_l_strlen(%rax)		# Verify string length is 0
	jne	notnullstr				# If not, not a null string
nullstr:
	#
	# We have a null string. Return value not really used but set it for comparison purposes
	#
	movl	$1, %eax
	jmp	done
notnullstr:
	#
	# We have either a non-null string or not a string at all.
	#
	movl	$0, %eax
done:
	addq	$8, %rsp				# Remove stack alignment bump
	cmpl	$0, %eax				# Set condition code for caller
	ret
	#
	# Here when input mval not defined. If undef_inhibit set, make the assumption the value is the NULL string.
	# Else, raise the undef error.
	#
undefmval:
	cmpb	$0, undef_inhibit(%rip)		# Test undef_inhibit setting
	jne	nullstr					# It's set, return as if was NULL
	movq	%rax, %rdi			# Move mval to arg register
	movb    $0, %al             		# Variable length argumentt
	call	underr					# Give undef error
	jmp	done					# Should never return but if do - at least return
# Below line is needed to avoid the ELF executable from ending up with an executable stack marking.
# This marking is not an issue in Linux but is in Windows Subsystem on Linux (WSL) which does not enable executable stack.
#ifndef __APPLE__
.section        .note.GNU-stack,"",@progbits
#endif
