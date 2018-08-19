/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
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

#ifndef SLEEP_H
#define SLEEP_H

#define	RESTART_FALSE	FALSE
#define	RESTART_TRUE	TRUE

#ifdef __APPLE__
#include "timing_mach.h"
#endif

/* Note: GT.M code *MUST NOT* use the sleep function because it causes problems with GT.M's timers on some platforms. Specifically,
 * the sleep function results in SIGARLM handler being silently deleted on Solaris systems (through Solaris 9 at least). This leads
 * to lost timer pops and has the potential for system hangs. The proper long sleep mechanism is hiber_start which can be accessed
 * through the LONG_SLEEP macro defined in mdef.h.
 *
 * On Linux boxes be sure to define USER_HZ macro (in gt_timers.c) appropriately to mitigate the timer clustering imposed by
 * the OS. Historically, the USER_HZ value has defaulted to 100 (same as HZ), thus resulting in at most 10ms accuracy when
 * delivering timed events.
 */

void m_usleep(int useconds);

#if !defined(__linux__) && !defined(__APPLE__)
#      error "Unsure of support for sleep functions on this platform"
#endif

/* Nonetheless, because we continue to press for the highest time discrimination available, where posible we use
 * clock_nanosleep and clock_gettime, which, while currently no faster than gettimeofday(), do eventually promise
 * sub-millisecond accuracy
 *
 * Because, as of this writing, in AIX the clock_* routines are so erratic with short times we use the functions above for most
 * things but give the following macro a separate name so AIX can use it in op_hang.c to ensure that a 1 second sleep always
 * puts the process in a different second as measured by $HOROLOG and the like.
 */
# define CLOCK_NANOSLEEP(NANOSECONDS, RESTART)							\
MBSTART {											\
	int 		STATUS;									\
	struct timespec	REQTIM;									\
												\
	assert(0 < (NANOSECONDS));								\
	clock_gettime(CLOCK_MONOTONIC, &REQTIM);						\
	if (NANOSECS_IN_SEC <= (NANOSECONDS) + REQTIM.tv_nsec)					\
	{											\
		REQTIM.tv_sec += (time_t)(((NANOSECONDS) + REQTIM.tv_nsec) / NANOSECS_IN_SEC);	\
		REQTIM.tv_nsec = ((NANOSECONDS) + REQTIM.tv_nsec) % NANOSECS_IN_SEC;		\
	}											\
	else											\
		REQTIM.tv_nsec += (long)(NANOSECONDS);						\
	do											\
	{											\
		STATUS = clock_nanosleep_abstime(&REQTIM);					\
		if (!RESTART || (0 == STATUS))							\
			break;									\
		assertpro (EINTR == STATUS);							\
	} while (TRUE);										\
} MBEND

# define SLEEP_USEC(MICROSECONDS, RESTART)					\
MBSTART {									\
	assert((MICROSECS_IN_SEC > MICROSECONDS) && (0 < MICROSECONDS));	\
	NANOSLEEP(((MICROSECONDS) * 1000), RESTART);				\
} MBEND

# define NANOSLEEP(NANOSECONDS, RESTART)				\
MBSTART {								\
	assert((NANOSECS_IN_SEC > NANOSECONDS) && (0 < NANOSECONDS));	\
	CLOCK_NANOSLEEP(NANOSECONDS, RESTART);				\
} MBEND

#endif /* SLEEP_H */
