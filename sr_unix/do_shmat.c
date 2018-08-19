/****************************************************************
 *								*
 * Copyright (c) 2001-2015 Fidelity National Information 	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* do_shmat.c UNIX - Attach shared memory (stub)
 *   Note:  The TANDEM (NonStop/UX) requires a special version of this routine
 *          so that attaching shared memory does not interfere with malloc.
 */

#include "mdef.h"

#include <sys/shm.h>
#include <sys/mman.h>
#include <errno.h>

#ifndef __APPLE__
#include <sys/sysmacros.h>
#endif
#include "gtm_stdio.h"
#include "gtm_ipc.h"

#include "do_shmat.h"

void *do_shmat(int4 shmid, const void *shm_base, int4 shmflg)
{
#	ifdef __sparc
	return(shmat((int)shmid, shm_base, shmflg | SHM_SHARE_MMU));
#	else
	return(shmat((int)shmid, shm_base, shmflg));
#	endif
}

/* This is do_shmat + capability to execute code from shared memory.
 * On platforms that support the SHM_EXEC bit (as of Oct 2014 this is only Linux) we use it.
 * On those that dont, we use mprotect (additional system call) on top of shmat.
 * Until SHM_EXEC is not available on all POSIX platforms that GT.M is built/supported on, we need the mprotect code.
 */
void *do_shmat_exec_perm(int4 shmid, size_t shm_size, int *save_errno)
{
	int4	shmflg;
	void	*addr;

#	if defined(SHM_EXEC)
	shmflg = SHM_EXEC;
#	else
	shmflg = 0;
#	endif
	addr = do_shmat(shmid, NULL, shmflg);
	if (-1 == (sm_long_t)addr)
	{
		*save_errno = errno;
		return addr;
	}
#	if !defined(SHM_EXEC)
	if (-1 == mprotect(addr, shm_size, PROT_READ | PROT_WRITE | PROT_EXEC))
	{
		*save_errno = errno;
		assert(FALSE);
		SHMDT(addr);
		addr = (void *)-1;
	}
#	endif
	return addr;
}
