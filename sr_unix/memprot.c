/****************************************************************
 *								*
 *	Copyright 2014 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/
#include <sys/mman.h>
#include "mdef.h"
#include "memprot.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

OS_PAGE_SIZE_DECLARE

/*
 * Ensure base address has at least <size> bytes of accessible space, followed by an inaccessible (PROT_NONE) page.
 */
void memprot(mstr *base, uint4 size)
{
	int	status;

	if (base->len < size)
	{
		if (NULL != base->addr)
			munmap(base->addr, base->len + OS_PAGE_SIZE);	/* clear previous allocation */
		base->len = (mstr_len_t)ROUND_UP(size, OS_PAGE_SIZE);
		base->addr = (char *)mmap(NULL, base->len + OS_PAGE_SIZE, PROT_READ | PROT_WRITE,
											MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (MAP_FAILED == base->addr)
		{
			base->len = 0;
			base->addr = NULL;
		} else
		{
			status = mprotect(base->addr + base->len, OS_PAGE_SIZE, PROT_NONE);
			if (-1 == status)
			{
				munmap(base->addr, base->len + OS_PAGE_SIZE);
				base->len = 0;
				base->addr = NULL;
			}
		}
	}
	return;
}
