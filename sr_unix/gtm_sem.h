/****************************************************************
 *								*
 * Copyright 2001, 2008 Fidelity Information Services, Inc	*
 *								*
 * Copyright (c) 2018 YottaDB LLC. and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef GTM_SEM_INCLUDED
#define GTM_SEM_INCLUDED

#define FTOK_SEM_PER_ID 3

#ifndef __APPLE__
union   semun {
	int     val;
	struct  semid_ds *buf;
	u_short *array;
#if defined(__linux__) && (__ia64)
	struct seminfo *__buf;		/* buffer for IPC_INFO */
	void *__pad;
#endif
} arg;
#endif

#endif /* GTM_SEM_INCLUDED */

#define GTM_SEM_CHECK_EINVAL(ydb_environment_init, save_errno, udi)                     \
{                                                                                       \
        assert(EINVAL != save_errno);                                                   \
        assert(0 <= udi->ftok_semid);                                                   \
        /* We want a core in case of EINVAL errno only if running in-house */           \
        if (ydb_environment_init && (EINVAL == save_errno))                             \
        {                                                                               \
                util_out_print("udi->ftok_semid is: !UL", TRUE, udi->ftok_semid);       \
                util_out_print("save_errno is     : !UL", TRUE, save_errno);            \
                GTMASSERT;                                                              \
        }                                                                               \
}
