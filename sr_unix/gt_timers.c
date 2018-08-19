/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.*
 * All rights reserved.						*
 *								*
 * Copyright (c) 2018 Stephen L Johnson.			*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* This file contains a general purpose timer package. Simultaneous multiple timers are supported.
 * All outstanding timers are contained in a queue of pending requests. New timer is added to the
 * queue in an expiration time order. The first timer in a queue expires first, and the last one
 * expires last. When the timer expires, the signal is generated and the process is awakened. This
 * timer is then removed from the queue, and the first timer in a queue is started again, and so on.
 * Starting a timer with the timer id equal to one of the existing timers in a chain will remove the
 * existing timer from the chain and add a new one instead.
 *
 * It is a responsibility of the user to go to hibernation mode by executing appropriate system call
 * if the user needs to wait for the timer expiration.
 *
 * Additionally, certain timers, designated by "safe" flag, can be processed---and, if necessary, out
 * of order---while we are deferred on interrupts. All regular timers that pop within the deferred
 * zone, will be handler in order as soon as we reenable interrupt processing.
 *
 * Following are top-level user-callable routines of this package:
 *
 * void sys_get_curr_time(ABS_TIME *atp)
 * 	fetch absolute time into stucture
 *
 * void hiber_start(uint4 hiber)
 *      used to sleep for hiber milliseconds
 *
 * void start_timer(TID tid, int4 time_to_expir, void (*handler)(), int4 dlen, char *data)
 *	Used to start a new timer.
 *
 * void cancel_timer(TID tid)
 *	Cancel an existing timer.
 *	Cancelling timer with tid = 0, cancels all timers.
 */

#include "mdef.h"

#include "gtm_signal.h"
#include "gtm_time.h"
#include "gtm_string.h"
#include "gtmimagename.h"

#include <errno.h>
#include <stddef.h>
#include <stdarg.h>

#if (defined(__ia64) && defined(__linux__)) || defined(__MVS__)
# include "gtm_unistd.h"
#endif /* __ia64 && __linux__ or __MVS__ */
#include "gt_timer.h"
#include "wake_alarm.h"
#ifdef DEBUG
# include "wbox_test_init.h"
# include "io.h"
#endif
#if	defined(mips) && !defined(_SYSTYPE_SVR4)
# include <bsd/sys/time.h>
#else
# include <sys/time.h>
#endif
#ifndef __MVS__
# include <sys/param.h>
#endif
#include "send_msg.h"
#include "gtmio.h"
#include "have_crit.h"
#include "util.h"
#include "sleep.h"
#include "error.h"
#include "gtm_multi_thread.h"
#include "gtmxc_types.h"

#ifdef ITIMER_REAL
# define BSD_TIMER
# define USER_HZ 1000
#else
/* check def of time() including arg - see below; should be time_t
 * (from sys/types.h) and traditionally unsigned long */
# ifndef __osf__
int4	time();
# endif
#endif

#define TIMER_BLOCK_SIZE	64	/* # of timer entries allocated initially as well as at every expansion */
#define GT_TIMER_EXPAND_TRIGGER	8	/* if the # of timer entries in the free queue goes below this, allocate more */
#define GT_TIMER_INIT_DATA_LEN	8
#define MAX_TIMER_POP_TRACE_SZ	32

#define ADD_SAFE_HNDLR(HNDLR)									\
{												\
	assert((ARRAYSIZE(safe_handlers) - 1) > safe_handlers_cnt);				\
	assert(NULL != (void *)HNDLR); /* void * to avoid warnings of always true */		\
	safe_handlers[safe_handlers_cnt++] = HNDLR;						\
}

#ifdef BSD_TIMER
#  define REPORT_SETITIMER_ERROR(TIMER_TYPE, SYS_TIMER, FATAL, ERRNO)				\
{												\
	char s[512];										\
												\
	SNPRINTF(s, 512, "Timer: %s; timer_active: %d; "					\
		"sys_timer.it_value: [tv_sec: %ld; tv_usec: %ld]; "				\
		"sys_timer.it_interval: [tv_sec: %ld; tv_usec: %ld]",				\
		TIMER_TYPE, timer_active,							\
		SYS_TIMER.it_value.tv_sec, SYS_TIMER.it_value.tv_usec,				\
		SYS_TIMER.it_interval.tv_sec, SYS_TIMER.it_interval.tv_usec);			\
	if (FATAL)										\
	{											\
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(7)						\
			ERR_SETITIMERFAILED, 1, ERRNO, ERR_TEXT, 2, LEN_AND_STR(s));		\
		in_setitimer_error = TRUE;							\
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_SETITIMERFAILED, 1, ERRNO);	\
	} else											\
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(7)	MAKE_MSG_WARNING(ERR_SETITIMERFAILED),	\
			1, ERRNO, ERR_TEXT, 2, LEN_AND_STR(s));					\
}

#define SYS_SETTIMER(TIMER, DELTA)								\
MBSTART {											\
	sys_timer_at = (TIMER)->expir_time;							\
	sys_settimer((TIMER)->tid, DELTA);							\
} MBEND

STATICDEF struct itimerval	sys_timer, old_sys_timer;
STATICDEF ABS_TIME		sys_timer_at;			/* Absolute time associated with sys_timer */
STATICDEF boolean_t		in_setitimer_error;
#endif

#define DUMMY_SIG_NUM		0		/* following can be used to see why timer_handler was called */
#define SAFE_FOR_ANY_TIMER	((INTRPT_OK_TO_INTERRUPT == intrpt_ok_state) && (FALSE == process_exiting) && !fast_lock_count)
/* In case threads are running, we dont want any unsafe timers to be handled during a timer handler pop. This is because we
 * dont know if the threads will modify the same global variable that the unsafe timer modifies concurrently.
 * But it is okay for timers to be started by individual threads. For example the iott_flush_timer will be started inside
 * thread code only while holding a mutex lock (e.g. inside gtm_putmsg_list or so) and even though a "setitimer" call is done
 * inside one thread, the SIGALRM pop will happen only in the parent process because all threads have SIGALRM disabled in their
 * signal mask. Define SAFE_FOR_TIMER_POP and SAFE_FOR_TIMER_START variables accordingly.
 */
#define	SAFE_FOR_TIMER_POP	(SAFE_FOR_ANY_TIMER && !multi_thread_in_use)
#define	SAFE_FOR_TIMER_START	(SAFE_FOR_ANY_TIMER)

STATICDEF volatile GT_TIMER *timeroot = NULL;	/* chain of pending timer requests in time order */
STATICDEF boolean_t first_timeset = TRUE;
STATICDEF struct sigaction prev_alrm_handler;	/* save previous SIGALRM handler, if any */

/* Chain of unused timer request blocks */
STATICDEF volatile	GT_TIMER	*timefree = NULL;
STATICDEF volatile 	int4		num_timers_free;		/* # of timers in the unused queue */
STATICDEF		int4		timeblk_hdrlen;
STATICDEF volatile 	st_timer_alloc	*timer_allocs = NULL;

STATICDEF int 		safe_timer_cnt, timer_pop_cnt;			/* Number of safe timers in queue/popped */
STATICDEF TID 		*deferred_tids;

STATICDEF timer_hndlr	safe_handlers[MAX_SAFE_TIMER_HNDLRS + 1];	/* +1 for NULL to terminate list */
STATICDEF int		safe_handlers_cnt;

STATICDEF boolean_t	stolen_timer = FALSE;	/* only complain once, used in check_for_timer_pops() */
STATICDEF char 		*whenstolen[] = {"check_for_timer_pops", "check_for_timer_pops first time"}; /* for check_for_timer_pops */

#ifdef DEBUG
STATICDEF int		trc_timerpop_idx;
STATICDEF GT_TIMER	trc_timerpop_array[MAX_TIMER_POP_TRACE_SZ];

# define TRACE_TIMER_POP(TIMER_INFO)							\
{											\
	memcpy(&trc_timerpop_array[trc_timerpop_idx], TIMER_INFO, SIZEOF(GT_TIMER));	\
	trc_timerpop_idx = (trc_timerpop_idx + 1) % MAX_TIMER_POP_TRACE_SZ;		\
}
#endif

/* Flag signifying timer is active. Especially useful when the timer handlers get nested. This has not been moved to a
 * threaded framework because we do not know how timers will be used with threads.
 */
GBLDEF	volatile boolean_t	timer_active = FALSE;
GBLDEF	volatile int4		timer_stack_count = 0;
GBLDEF	volatile boolean_t	timer_in_handler = FALSE;
GBLDEF	void			(*wcs_clean_dbsync_fptr)();	/* Reference to wcs_clean_dbsync() to be used in gt_timers.c. */
GBLDEF	void			(*wcs_stale_fptr)();		/* Reference to wcs_stale() to be used in gt_timers.c. */

GBLREF	boolean_t	blocksig_initialized;			/* Set to TRUE when blockalrm, block_ttinout, and block_sigsent are
								 * initialized. */
GBLREF	sigset_t	blockalrm;
GBLREF	sigset_t	block_ttinout;
GBLREF	sigset_t	block_sigsent;
GBLREF	sigset_t	block_worker;
GBLREF 	volatile int4	fast_lock_count;
GBLREF	boolean_t	oldjnlclose_started;
GBLREF	void		(*jnl_file_close_timer_ptr)(void);	/* Initialized only in gtm_startup(). */
GBLREF	int4		error_condition;
GBLREF	int4		outofband;
GBLREF	int		process_exiting;
#ifdef DEBUG
GBLREF	boolean_t	in_nondeferrable_signal_handler;
GBLREF	boolean_t	gtm_jvm_process;
#endif

error_def(ERR_SETITIMERFAILED);
error_def(ERR_TEXT);
error_def(ERR_TIMERHANDLER);

/* Called when a hiber_start timer pops. Set flag so a given timer will wake up (not go back to sleep). */
STATICFNDEF void hiber_wake(TID tid, int4 hd_len, int4 **waitover_flag)
{
	**waitover_flag = TRUE;
}

/* Preallocate some memory for timers. */
void gt_timers_alloc(void)
{
	int4		gt_timer_cnt;
	GT_TIMER	*timeblk, *timeblks;
	st_timer_alloc	*new_alloc;

	/* Allocate timer blocks putting each timer on the free queue */
	assert(1 > timer_stack_count);
	timeblk_hdrlen = OFFSETOF(GT_TIMER, hd_data[0]);
	timeblk = timeblks = (GT_TIMER *)malloc((timeblk_hdrlen + GT_TIMER_INIT_DATA_LEN) * TIMER_BLOCK_SIZE);
	new_alloc = (st_timer_alloc *)malloc(SIZEOF(st_timer_alloc));
	new_alloc->addr = timeblk;
	new_alloc->next = (st_timer_alloc *)timer_allocs;
	timer_allocs = new_alloc;
	for (gt_timer_cnt = TIMER_BLOCK_SIZE; 0 < gt_timer_cnt; --gt_timer_cnt)
	{
		timeblk->hd_len_max = GT_TIMER_INIT_DATA_LEN;	/* Set amount it can store */
		timeblk->next = (GT_TIMER *)timefree;		/* Put on free queue */
		timefree = timeblk;
		timeblk = (GT_TIMER *)((char *)timeblk + timeblk_hdrlen + GT_TIMER_INIT_DATA_LEN);	/* Next! */
	}
	assert(((char *)timeblk - (char *)timeblks) == (timeblk_hdrlen + GT_TIMER_INIT_DATA_LEN) * TIMER_BLOCK_SIZE);
	num_timers_free += TIMER_BLOCK_SIZE;
}

void add_safe_timer_handler(int safetmr_cnt, ...)
{
	int		i;
	va_list		var;
	timer_hndlr	tmrhndlr;

	VAR_START(var, safetmr_cnt);
	for (i = 1; i <= safetmr_cnt; i++)
	{
		tmrhndlr = va_arg(var, timer_hndlr);
		ADD_SAFE_HNDLR(tmrhndlr);
	}
	va_end(var);
}

/* Do the initialization of blockalrm, block_ttinout and block_sigsent, and set blocksig_initialized to TRUE, so
 * that we can later block signals when there is a need. This function should be called very early
 * in the main() routines of modules that wish to do their own interrupt handling.
 */
void set_blocksig(void)
{
	sigemptyset(&blockalrm);
	sigaddset(&blockalrm, SIGALRM);
	sigemptyset(&block_ttinout);
	sigaddset(&block_ttinout, SIGTTIN);
	sigaddset(&block_ttinout, SIGTTOU);
	sigemptyset(&block_sigsent);
	sigaddset(&block_sigsent, SIGINT);
	sigaddset(&block_sigsent, SIGQUIT);
	sigaddset(&block_sigsent, SIGTERM);
	sigaddset(&block_sigsent, SIGTSTP);
	sigaddset(&block_sigsent, SIGCONT);
	sigaddset(&block_sigsent, SIGALRM);
	sigfillset(&block_worker);
	sigdelset(&block_worker, SIGSEGV);
	sigdelset(&block_worker, SIGKILL);
	sigdelset(&block_worker, SIGFPE);
	sigdelset(&block_worker, SIGBUS);
	blocksig_initialized = TRUE;	/* note the fact that blockalrm and block_sigsent are initialized */
}

/* Initialize group of timer blocks */
void prealloc_gt_timers(void)
{	/* Preallocate some timer blocks. This will be all the timer blocks we hope to need.
	 * Allocate them with 8 bytes of possible data each.
	 * If more timer blocks are needed, we will allocate them as needed.
	 */
	gt_timers_alloc();
	/* Now initialize the safe timers. Must be done dynamically to avoid the situation where this module always references all
	 * possible safe timers thus pulling extra stuff into executables that don't need or want it.
	 *
	 * First step, fill in the safe timers contained within this module which are always available.
	 */
	ADD_SAFE_HNDLR(&hiber_wake);		/* Resident in this module */
	ADD_SAFE_HNDLR(&hiber_start_wait_any);	/* Resident in this module */
	ADD_SAFE_HNDLR(&wake_alarm);		/* Standalone module containing only one global reference */
}

/* Get current clock time. Fill-in the structure with the absolute time of system clock.
 * Arguments:	atp - pointer to structure of absolute time
 */
void sys_get_curr_time(ABS_TIME *atp)
{
#	ifdef BSD_TIMER
	struct timeval	tv;
	struct timespec	elp_time;

	/* Note: This function is called from timer_handler and so needs to be async-signal safe.
	 * POSIX defines "clock_gettime" as safe but not "gettimeofday" so dont use the latter.
	 */
	clock_gettime(CLOCK_REALTIME, &elp_time);
	atp->at_sec = (int4)elp_time.tv_sec;
	atp->at_usec = (int4)elp_time.tv_nsec / 1000;
#	else
	atp->at_sec = time((int4 *) 0);
	atp->at_usec = 0;
#	endif
}

/* Start hibernating by starting a timer and waiting for it. */
void hiber_start(uint4 hiber)
{
	int4		waitover;
	int4		*waitover_addr;
	TID		tid;
	sigset_t	savemask;
	int		rc;

	SIGPROCMASK(SIG_BLOCK, &blockalrm, &savemask, rc);	/* block SIGALRM signal */
	/* sigsuspend() sets the signal mask to 'savemask' and waits for an ALARM signal. If the SIGALRM is a member of savemask,
	 * this process will never receive SIGALRM, and it will hang indefinitely. One such scenario would be if we interrupted a
	 * timer handler with kill -15, thus getting all timer setup reset by generic_signal_handler, and the gtm_exit_handler
	 * ended up invoking hiber_start (when starting gtmsecshr server, for instance). In such situations rely on something other
	 * than GT.M timers.
	 */
	if (sigismember(&savemask, SIGALRM))
	{	/* normally, if SIGALRMs are blocked, we must already be inside a timer handler, but someone can actually disable
		 * SIGALRMs, in which case we do not want this assert to trip in pro */
		assert(1 <= timer_stack_count);
		SLEEP_USEC((long long)(hiber * 1000LL), TRUE);
	} else
	{
		assertpro(1 > timer_stack_count);	/* if SIGALRMs are not blocked, we cannot be inside a timer handler */
		waitover = FALSE;			/* when OUR timer pops, it will set this flag */
		waitover_addr = &waitover;
		tid = (TID)waitover_addr;		/* unique id of this timer */
		start_timer_int((TID)tid, hiber, hiber_wake, SIZEOF(waitover_addr), &waitover_addr, TRUE);
		/* we will loop here until OUR timer pops and sets OUR flag */
		do
		{
			assert(!sigismember(&savemask, SIGALRM));
			sigsuspend(&savemask);		/* unblock SIGALRM and wait for timer interrupt */
			if (outofband)
			{
				cancel_timer(tid);
				break;
			}
		} while (FALSE == waitover);
	}
	SIGPROCMASK(SIG_SETMASK, &savemask, NULL, rc);	/* reset signal handlers */
}

/* Hibernate by starting a timer and waiting for it or any other timer to pop. */
void hiber_start_wait_any(uint4 hiber)
{
	sigset_t	savemask;
	int		rc;

	if (1000 > hiber)
	{
		SHORT_SLEEP(hiber);			/* note: some platforms call hiber_start */
		return;
	}
	assertpro(1 > timer_stack_count);		/* timer services are unavailable from within a timer handler */
	SIGPROCMASK(SIG_BLOCK, &blockalrm, &savemask, rc);	/* block SIGALRM signal and set new timer */
	/* Even though theoretically it is possible for any signal other than SIGALRM to discontinue the wait in sigsuspend,
	 * the intended use of this function targets only timer-scheduled events. For that reason, assert that SIGALRMs are
	 * not blocked prior to scheduling a timer, whose delivery we will be waiting upon, as otherwise we might end up
	 * waiting indefinitely. Note, however, that the use of SLEEP_USEC in hiber_start, explained in the accompanying
	 * comment, should not be required in hiber_start_wait_any, as we presently do not invoke this function in interrupt-
	 * induced code, and so we should not end up here with SIGALARMs blocked.
	 */
	assert(!sigismember(&savemask, SIGALRM));
	start_timer_int((TID)hiber_start_wait_any, hiber, NULL, 0, NULL, TRUE);
	sigsuspend(&savemask);				/* unblock SIGALRM and wait for timer interrupt */
	cancel_timer((TID)hiber_start_wait_any);	/* cancel timer block before reenabling */
	SIGPROCMASK(SIG_SETMASK, &savemask, NULL, rc);	/* reset signal handlers */
}

/* Wrapper function for start_timer() that is exposed for outside use. The function ensures that time_to_expir is positive. If
 * negative value or 0 is passed, set time_to_expir to 0 and invoke start_timer(). The reason we have not merged this functionality
 * with start_timer() is because there is no easy way to determine whether the function is invoked from inside GT.M or by an
 * external routine.
 * Arguments:	tid 		- timer id
 *		time_to_expir	- time to expiration in msecs
 *		handler		- pointer to handler routine
 *		hdata_len       - length of handler data next arg
 *		hdata		- data to pass to handler (if any)
 */
void gtm_start_timer(TID tid,
		 int4 time_to_expir,
		 void (*handler)(),
		 int4 hdata_len,
		 void *hdata)
{
	if (0 >= time_to_expir)
		time_to_expir = 0;
	start_timer(tid, time_to_expir, handler, hdata_len, hdata);
}

/* Start the timer. If timer chain is empty or this is the first timer to expire, actually start the system timer.
 * Arguments:	tid 		- timer id
 *		time_to_expir	- time to expiration in msecs
 *		handler		- pointer to handler routine
 *      	hdata_len       - length of handler data next arg
 *      	hdata		- data to pass to handler (if any)
 */
void start_timer(TID tid, int4 time_to_expir, void (*handler)(), int4 hdata_len, void *hdata)
{
	sigset_t	savemask;
	boolean_t	safe_timer = FALSE, safe_to_add = FALSE;
	int		i, rc;

	assertpro(0 <= time_to_expir);			/* Callers should verify non-zero time */
	DUMP_TIMER_INFO("At the start of start_timer()");
	if (NULL == handler)
	{
		safe_to_add = TRUE;
		safe_timer = TRUE;
	} else if (wcs_clean_dbsync_fptr == handler)
	{	/* Account for known instances of the above function being called from within a deferred zone. */
		assert((INTRPT_OK_TO_INTERRUPT == intrpt_ok_state) || (INTRPT_IN_WCS_WTSTART == intrpt_ok_state)
			|| (INTRPT_IN_GDS_RUNDOWN == intrpt_ok_state) || (INTRPT_IN_DB_CSH_GETN == intrpt_ok_state));
		safe_to_add = TRUE;
	} else if (wcs_stale_fptr == handler)
	{	/* Account for known instances of the above function being called from within a deferred zone. */
		assert((INTRPT_OK_TO_INTERRUPT == intrpt_ok_state) || (INTRPT_IN_DB_CSH_GETN == intrpt_ok_state)
			|| (INTRPT_IN_TRIGGER_NOMANS_LAND == intrpt_ok_state));
		safe_to_add = TRUE;
	} else
	{
		for (i = 0; NULL != safe_handlers[i]; i++)
		{
			if (safe_handlers[i] == handler)
			{
				safe_to_add = TRUE;
				safe_timer = TRUE;
				break;
			}
		}
	}
	if (!safe_to_add && !SAFE_FOR_TIMER_START)
	{
		assert(FALSE);
		return;
	}
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_BLOCK, &blockalrm, &savemask, rc);	/* block SIGALRM signal */
	start_timer_int(tid, time_to_expir, handler, hdata_len, hdata, safe_timer);
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_SETMASK, &savemask, NULL, rc);		/* reset signal handlers */
	DUMP_TIMER_INFO("At the end of start_timer()");
}

/* Internal version of start_timer that does not protect itself, assuming this has already been done.
 * Otherwise does as explained above in start_timer.
 */
STATICFNDEF void start_timer_int(TID tid, int4 time_to_expir, void (*handler)(), int4 hdata_len, void *hdata, boolean_t safe_timer)
{
	ABS_TIME	at;
	GT_TIMER 	*newt;

 	assert(0 <= time_to_expir);
	sys_get_curr_time(&at);
	if (first_timeset)
	{
		init_timers();
		first_timeset = FALSE;
	}
	/* We expect no timer with id=<tid> to exist in the timer queue currently. This is asserted in "add_timer" call below.
	 * In pro though, we'll be safe and remove any tids that exist before adding a new entry with the same tid - 2009/10.
	 * If a few years pass without the assert failing, it might be safe then to remove the PRO_ONLY code below.
	 */
#	ifndef DEBUG
	remove_timer(tid); /* Remove timer from chain */
#	endif
	/* Check if # of free timer slots is less than minimum threshold. If so, allocate more of those while it is safe to do so */
	if ((GT_TIMER_EXPAND_TRIGGER > num_timers_free) && (1 > timer_stack_count))
		gt_timers_alloc();
	DUMP_TIMER_INFO("Before invoking add_timer()");
	newt = add_timer(&at, tid, time_to_expir, handler, hdata_len, hdata, safe_timer);	/* Put new timer in the queue. */
	DUMP_TIMER_INFO("After invoking add_timer()");
	if ((timeroot->tid == tid) || !timer_active
			|| (timer_active
				&& ((newt->expir_time.at_sec < sys_timer_at.at_sec)
					|| ((newt->expir_time.at_sec == sys_timer_at.at_sec)
						&& ((gtm_tv_usec_t)newt->expir_time.at_usec < sys_timer_at.at_usec)))))
		start_first_timer(&at);
}

/* Cancel timer.
 * Arguments:	tid - timer id
 */
void cancel_timer(TID tid)
{
	ABS_TIME	at;
	sigset_t	savemask;
	boolean_t	first_timer;
	int		rc;

	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_BLOCK, &blockalrm, &savemask, rc);	/* block SIGALRM signal */
	DUMP_TIMER_INFO("At the start of cancel_timer()");
	sys_get_curr_time(&at);
	first_timer = (timeroot && (timeroot->tid == tid));
	remove_timer(tid);		/* remove it from the chain */
	if (first_timer)
	{
		if (timeroot)
			start_first_timer(&at);		/* start the first timer in the chain */
		else if (timer_active)
			sys_canc_timer();
	}
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_SETMASK, &savemask, NULL, rc);
	DUMP_TIMER_INFO("At the end of cancel_timer()");
}

/* Clear the timers' state for the forked-off process. */
void clear_timers(void)
{
	sigset_t	savemask;
	int		rc;

	DUMP_TIMER_INFO("At the start of clear_timers()");
	if (NULL == timeroot)
	{	/* If no timers have been initialized in this process, take fast path (avoid system call) */
		/* If the only timer popped, and we got a SIGTERM while its handler was active, the timeroot
		 * would be NULL and timer_in_handler would be TRUE, but that should be safe for the fast path,
		 * so allow this case if the process is exiting.
		 */
		assert((FALSE == timer_in_handler) || process_exiting);
		assert(FALSE == timer_active);
		assert(FALSE == oldjnlclose_started);
		assert(FALSE == GET_DEFERRED_TIMERS_CHECK_NEEDED);
		return;
	}
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_BLOCK, &blockalrm, &savemask, rc);	/* block SIGALRM signal */
	while (timeroot)
		remove_timer(timeroot->tid);
	timer_in_handler = FALSE;
	timer_active = FALSE;
	oldjnlclose_started = FALSE;
	CLEAR_DEFERRED_TIMERS_CHECK_NEEDED;
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_SETMASK, &savemask, NULL, rc);
	DUMP_TIMER_INFO("After invoking clear_timers()");
	return;
}

/* System call to set timer. Time is given im msecs.
 * Arguments:	tid		- timer id
 *		time_to_expir	- time to expiration
 */
STATICFNDEF void sys_settimer(TID tid, ABS_TIME *time_to_expir)
{
#	ifdef BSD_TIMER
	if (in_setitimer_error)
		return;
	sys_timer.it_value.tv_sec = time_to_expir->at_sec;
	sys_timer.it_value.tv_usec = (gtm_tv_usec_t)time_to_expir->at_usec;
	sys_timer.it_interval.tv_sec = sys_timer.it_interval.tv_usec = 0;
	assert(1000000 > sys_timer.it_value.tv_usec);
	if ((-1 == setitimer(ITIMER_REAL, &sys_timer, &old_sys_timer)) || WBTEST_ENABLED(WBTEST_SETITIMER_ERROR))
	{
		REPORT_SETITIMER_ERROR("ITIMER_REAL", sys_timer, TRUE, errno);
	}
#	else
	if (time_to_expir->at_sec == 0)
		alarm((unsigned)1);
	else
		alarm(time_to_expir->at_sec);
#	endif
	timer_active = TRUE;
}

/* Start the first timer in the timer chain
 * Arguments:	curr_time	- current time assumed within the function
 */
STATICFNDEF void start_first_timer(ABS_TIME *curr_time)
{
	ABS_TIME	eltime;
	GT_TIMER	*tpop;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	DUMP_TIMER_INFO("At the start of start_first_timer()");
	CLEAR_DEFERRED_TIMERS_CHECK_NEEDED;
	if ((1 < timer_stack_count) || (TRUE == timer_in_handler))
		return;
	for (tpop = (GT_TIMER *)timeroot ; tpop ; tpop = tpop->next)
	{
		eltime = sub_abs_time((ABS_TIME *)&tpop->expir_time, curr_time);
		if ((0 > eltime.at_sec) || ((0 == eltime.at_sec) && (0 == eltime.at_usec)))
		{	/* Timer has expired. Handle safe timers, defer unsafe timers. */
			if (tpop->safe || (SAFE_FOR_TIMER_START && (1 > timer_stack_count)
						&& !(TREF(in_ext_call) && (wcs_stale_fptr == tpop->handler))))
			{
				timer_handler(DUMMY_SIG_NUM);
				/* At this point all timers should have been handled, including a recursive call to
				 * start_first_timer(), if needed, and SET_DEFERRED_TIMERS_CHECK_NEEDED invoked if
				 * appropriate, so we are done.
				 */
				break;
			} else
			{
				SET_DEFERRED_TIMERS_CHECK_NEEDED;
				tpop->block_int = intrpt_ok_state;
			}
		} else
		{	/* Set system timer to wake on unexpired timer. */
			SYS_SETTIMER(tpop, &eltime);
			break;	/* System timer will handle subsequent timers, so we are done. */
		}
		assert(GET_DEFERRED_TIMERS_CHECK_NEEDED);
	}
	assert(timeroot || !GET_DEFERRED_TIMERS_CHECK_NEEDED);
	DUMP_TIMER_INFO("At the end of start_first_timer()");
}

/* Timer handler. This is the main handler routine that is being called by the kernel upon receipt
 * of timer signal. It dispatches to the user handler routine, and removes first timer in a timer
 * queue. If the queue is not empty, it starts the first timer in the queue. The why parameter is a
 * no-op in our case, but is required to maintain compatibility with the system type of __sighandler_t,
 * which is (void*)(int).
 */
STATICFNDEF void timer_handler(int why)
{
	int4		cmp, save_error_condition;
	GT_TIMER	*tpop, *tpop_prev = NULL;
	ABS_TIME	at;
	int		save_errno, timer_defer_cnt, offset;
	TID 		*deferred_tid;
	boolean_t	tid_found;
	char 		*save_util_outptr;
	va_list		save_last_va_list_ptr;
	boolean_t	util_copy_saved = FALSE, safe_for_timer_pop;
#	ifdef DEBUG
	boolean_t	save_in_nondeferrable_signal_handler;
	ABS_TIME	rel_time, old_at, late_time;
	static int	last_continue_proc_cnt = -1;
#	endif
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	assert(gtm_is_main_thread() || gtm_jvm_process);
	DUMP_TIMER_INFO("At the start of timer_handler()");
	if (SIGALRM == why)
	{	/* If why is 0, we know that timer_handler() was called directly, so no need
		 * to check if the signal needs to be forwarded to appropriate thread.
		 */
		FORWARD_SIG_TO_MAIN_THREAD_IF_NEEDED(SIGALRM);
	}
#	ifdef DEBUG
	/* Note that it is possible "in_nondeferrable_signal_handler" is non-zero if we first went into generic_signal_handler
	 * (say to handle sig-3) and then had a timer handler pop while inside there (possible for example in receiver server).
	 * So save current value of global and restore it at end of this function.
	 */
	save_in_nondeferrable_signal_handler = in_nondeferrable_signal_handler;
#	endif
	/* timer_handler() may or may not be protected from signals.
	 * If why is SIGALRM, the OS typically blocks SIGALRM while this handler is executing.
	 * If why is DUMMY_SIG_NUM, SIGALRM is not blocked, so make sure that a concurrent SIGALRM bails out at this point.
	 * All other routines which manipulate the timer data structure block SIGALRM (using SIGPROCMASK), so timer_handler()
	 * can't conflict with them. As long as those routines can't be invoked asynchronously while timer_handler (or another
	 * of those routines) is running, there can be no conflict, and the timer structures are safe from concurrent manipulation.
	 */
	if (1 < INTERLOCK_ADD(&timer_stack_count, UNUSED, 1))
	{
		SET_DEFERRED_TIMERS_CHECK_NEEDED;
		INTERLOCK_ADD(&timer_stack_count, UNUSED, -1);
		return;
	}
	CLEAR_DEFERRED_TIMERS_CHECK_NEEDED;
	save_errno = errno;
	save_error_condition = error_condition;	/* aka SIGNAL */
	timer_active = FALSE;				/* timer has popped; system timer not active anymore */
	sys_get_curr_time(&at);
	tpop = (GT_TIMER *)timeroot;
	timer_defer_cnt = 0;				/* reset the deferred timer count, since we are in timer_handler */
	safe_for_timer_pop = SAFE_FOR_TIMER_POP;
	/* If "multi_thread_in_use" is TRUE, it is possible util_out* buffers are concurrently being manipulated by the running
	 * threads. So do not use SAVE/RESTORE_UTIL_OUT_BUFFER macros. Thankfully in this case, "safe_for_timer_pop" will
	 * be FALSE (asserted below) and so only safe timer handlers will be driven. We expect the safe timer handlers to
	 * not play with the util_out* buffers. So it is actually okay to not do the SAVE/RESTORE_UTIL_OUT_BUFFER.
	 */
	assert(!multi_thread_in_use || !safe_for_timer_pop);
	if (safe_for_timer_pop)
		SAVE_UTIL_OUT_BUFFER(save_util_outptr, save_last_va_list_ptr, util_copy_saved);
#	ifdef DEBUG
	if (safe_for_timer_pop)
		in_nondeferrable_signal_handler = IN_TIMER_HANDLER;
	/* Allow a base 50 seconds of lateness for safe timers */
	late_time.at_sec = 50;
	late_time.at_usec = 0;
#	endif
	while (tpop)					/* fire all handlers that expired */
	{
		cmp = abs_time_comp(&at, (ABS_TIME *)&tpop->expir_time);
		if (cmp < 0)
			break;
#		if defined(DEBUG) && !defined(_AIX) && !defined(__armv6l__) && !defined(__armv7l__) && !defined(__APPLE__)
		if (tpop->safe && (TREF(continue_proc_cnt) == last_continue_proc_cnt)
			&& !(ydb_white_box_test_case_enabled
				&& ((WBTEST_SIGTSTP_IN_JNL_OUTPUT_SP == ydb_white_box_test_case_number)
					|| (WBTEST_EXPECT_IO_HANG == ydb_white_box_test_case_number))))
		{	/* Check if the timer is extremely overdue, with the following exceptions:
			 *	- Unsafe timers can be delayed indefinitely.
			 *	- AIX and ARM systems tend to arbitrarily delay processes when loaded.
			 *	- WBTEST_SIGTSTP_IN_JNL_OUTPUT_SP stops the process from running.
			 *	- Some other mechanism causes a SIGSTOP/SIGCONT, bumping continue_proc_cnt.
			 */
			rel_time = sub_abs_time(&at, (ABS_TIME *)&tpop->expir_time);
			if (abs_time_comp(&late_time, &rel_time) <= 0)
				gtm_fork_n_core();	/* Dump core, but keep going. */
		}
		last_continue_proc_cnt = TREF(continue_proc_cnt);
#		endif
		/* A timer might pop while we are in the non-zero intrpt_ok_state zone, which could cause collisions. Instead,
		 * we will defer timer events and drive them once the deferral is removed, unless the timer is safe.
		 * Handle wcs_stale timers during external calls similarly.
		 */
		if ((safe_for_timer_pop && !(TREF(in_ext_call) && (wcs_stale_fptr == tpop->handler))) || tpop->safe)
		{
			if (NULL != tpop_prev)
				tpop_prev->next = tpop->next;
			else
				timeroot = tpop->next;
			if (tpop->safe)
			{
				safe_timer_cnt--;
				assert(0 <= safe_timer_cnt);
			}
			if (NULL != tpop->handler)	/* if there is a handler, call it */
			{
#				ifdef DEBUG
				if (ydb_white_box_test_case_enabled
					&& (WBTEST_DEFERRED_TIMERS == ydb_white_box_test_case_number)
					&& ((void *)tpop->handler != (void*)jnl_file_close_timer_ptr))
				{
					DBGFPF((stderr, "TIMER_HANDLER: handled a timer\n"));
					timer_pop_cnt++;
				}
#				endif
				timer_in_handler = TRUE;
				(*tpop->handler)(tpop->tid, tpop->hd_len, tpop->hd_data);
				timer_in_handler = FALSE;
				if (!tpop->safe)		/* if safe, avoid a system call */
				{
					DEBUG_ONLY(old_at = at);
					sys_get_curr_time(&at);	/* refresh current time if called a handler */
#					ifdef DEBUG
					/* Include the time it took to handle the unsafe timer in the allowed late time.
					 * Otherwise, a hung unsafe timer could cause a subsequent safe timer to be overdue.
					 */
					rel_time = sub_abs_time(&at, &old_at);
					late_time.at_sec += rel_time.at_sec;
					late_time.at_usec += rel_time.at_usec;
					if (late_time.at_usec > MICROSECS_IN_SEC)
					{
						late_time.at_sec++;
						late_time.at_usec -= MICROSECS_IN_SEC;
					}
#					endif
				}
				DEBUG_ONLY(TRACE_TIMER_POP(tpop));
			}
			tpop->next = (GT_TIMER *)timefree;	/* put timer block on the free chain */
			timefree = tpop;
			if (NULL != tpop_prev)
				tpop = tpop_prev->next;
			else
				tpop = (GT_TIMER *)timeroot;
			num_timers_free++;
			assert(0 < num_timers_free);
		} else
		{
			timer_defer_cnt++;
#			ifdef DEBUG
			if (ydb_white_box_test_case_enabled
				&& (WBTEST_DEFERRED_TIMERS == ydb_white_box_test_case_number))
			{
				if (!deferred_tids)
				{
					deferred_tids = (TID *)malloc(SIZEOF(TID) * 2);
					*deferred_tids = tpop->tid;
					*(deferred_tids + 1) = -1;
					DBGFPF((stderr, "TIMER_HANDLER: deferred a timer\n"));
				} else
				{
					tid_found = FALSE;
					deferred_tid = deferred_tids;
					while (-1 != *deferred_tid)
					{
						if (*deferred_tid == tpop->tid)
						{
							tid_found = TRUE;
							break;
						}
						deferred_tid++;
					}
					if (!tid_found)
					{
						offset = deferred_tid - deferred_tids;
						deferred_tid = (TID *)malloc((offset + 2) * SIZEOF(TID));
						memcpy(deferred_tid, deferred_tids, offset * SIZEOF(TID));
						free(deferred_tids);
						deferred_tids = deferred_tid;
						*(deferred_tids + offset++) = tpop->tid;
						*(deferred_tids + offset) = -1;
						DBGFPF((stderr, "TIMER_HANDLER: deferred a timer\n"));
					}
				}
			}
#			endif
			tpop->block_int = intrpt_ok_state;
			tpop_prev = tpop;
			tpop = tpop->next;
			if ((0 == safe_timer_cnt) && !(TREF(in_ext_call) && (wcs_stale_fptr == tpop_prev->handler)))
				break;		/* no more safe timers left, and not special case, so quit */
		}
	}
	if (safe_for_timer_pop)
		RESTORE_UTIL_OUT_BUFFER(save_util_outptr, save_last_va_list_ptr, util_copy_saved);
	if (safe_for_timer_pop || (0 < safe_timer_cnt))
		start_first_timer(&at);
	else if ((NULL != timeroot) || (0 < timer_defer_cnt))
		SET_DEFERRED_TIMERS_CHECK_NEEDED;
	/* Restore mainline error_condition global variable. This way any gtm_putmsg or rts_errors that occurred inside interrupt
	 * code do not affect the error_condition global variable that mainline code was relying on. For example, not doing this
	 * restore caused the update process (in updproc_ch) to issue a GTMASSERT (GTM-7526). BYPASSOK.
	 */
	SET_ERROR_CONDITION(save_error_condition);	/* restore error_condition & severity */
	errno = save_errno;			/* restore mainline errno by similar reasoning as mainline error_condition */
	INTERLOCK_ADD(&timer_stack_count, UNUSED, -1);
#	ifdef DEBUG
	if (safe_for_timer_pop)
		in_nondeferrable_signal_handler = save_in_nondeferrable_signal_handler;
#	endif
	DUMP_TIMER_INFO("At the end of timer_handler()");
}

/* Find a timer given by tid in the timer chain.
 * Arguments:	tid	- timer id
 *		tprev	- address of pointer to previous node
 * Return:	pointer to timer in the chain, or 0 if timer is not found
 * Note:	tprev is set to the link previous to the tid link
 */
STATICFNDEF GT_TIMER *find_timer(TID tid, GT_TIMER **tprev)
{
	GT_TIMER *tc;

	tc = (GT_TIMER *)timeroot;
	*tprev = NULL;
	while (tc)
	{
		if (tc->tid == tid)
			return tc;
		*tprev = tc;
		tc = tc->next;
	}
	return 0;
}

/* Add timer to timer chain. Allocate a new link for a timer. Convert time to expiration into absolute time.
 * Insert new link into chain in timer order.
 * Arguments:	tid		- timer id
 *		time_to_expir	- elapsed time to expiration
 *		handler		- pointer to handler routine
 *      	hdata_len       - length of data to follow
 *      	hdata   	- data to pass to timer rtn if any
 *      	safe_timer	- timer's handler is in safe_handlers array
 */
STATICFNDEF GT_TIMER *add_timer(ABS_TIME *atp, TID tid, int4 time_to_expir, void (*handler)(), int4 hdata_len,
	void *hdata, boolean_t safe_timer)
{
	GT_TIMER	*tp, *tpp, *ntp, *lastntp;
	int4		cmp, i;
	st_timer_alloc	*new_alloc;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	/* assert that no timer entry with the same "tid" exists in the timer chain */
	assert(NULL == find_timer(tid, &tpp));
	/* obtain a new timer block */
	ntp = (GT_TIMER *)timefree;
	lastntp = NULL;
	for ( ; NULL != ntp; )
	{	/* we expect all callers of timer functions to not require more than 8 bytes of data; any violations
		 * of this assumption need to be caught---hence the assert below
		 */
		assert(GT_TIMER_INIT_DATA_LEN == ntp->hd_len_max);
		assert(ntp->hd_len_max >= hdata_len);
		if (ntp->hd_len_max >= hdata_len)	/* found one that can hold our data */
		{	/* dequeue block */
			if (NULL == lastntp)		/* first one on queue */
				timefree = ntp->next;	/* dequeue 1st element */
			else				/* is not 1st on queue -- use simple dequeue */
				lastntp->next = ntp->next;
			assert(0 < num_timers_free);
			num_timers_free--;
			break;
		}
		lastntp = ntp;	/* still looking, try next block */
		ntp = ntp->next;
	}
	/* if didn't find one, fail if dbg; else malloc a new one */
	if (NULL == ntp)
	{
		assert(FALSE);							/* if dbg, we should have enough already */
		ntp = (GT_TIMER *)malloc(timeblk_hdrlen + hdata_len);		/* if we are in a timer, malloc may error out */
		new_alloc = (st_timer_alloc *)malloc(SIZEOF(st_timer_alloc));	/* insert in front of the list */
		new_alloc->addr = ntp;
		new_alloc->next = (st_timer_alloc *)timer_allocs;
		timer_allocs = new_alloc;
		ntp->hd_len_max = hdata_len;
	}
	ntp->tid = tid;
	ntp->handler = handler;
	if (safe_timer)
	{
		ntp->safe = TRUE;
		safe_timer_cnt++;
		assert(0 < safe_timer_cnt);
	} else
		ntp->safe = FALSE;
	ntp->block_int = INTRPT_OK_TO_INTERRUPT;
	ntp->hd_len = hdata_len;
	if (0 < hdata_len)
		memcpy(ntp->hd_data, hdata, hdata_len);
	add_int_to_abs_time(atp, time_to_expir, &ntp->expir_time);
	ntp->start_time.at_sec = atp->at_sec;
	ntp->start_time.at_usec = atp->at_usec;
	tp = (GT_TIMER *)timeroot;
	tpp = NULL;
	while (tp)
	{
		cmp = abs_time_comp(&tp->expir_time, &ntp->expir_time);
		if (cmp >= 0)
			break;
		tpp = tp;
		tp = tp->next;
	}
	ntp->next = tp;
	if (NULL == tpp)
		timeroot = ntp;
	else
		tpp->next = ntp;
	return ntp;
}

/* Remove timer from the timer chain. */
STATICFNDEF void remove_timer(TID tid)
{
	GT_TIMER *tprev, *tp, *tpp;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	DUMP_TIMER_INFO("At the start of remove_timer()");
	if (tp = find_timer(tid, &tprev))		/* Warning: assignment */
	{
		if (tprev)
			tprev->next = tp->next;
		else
		{
			timeroot = tp->next;
			if (NULL == timeroot)
				CLEAR_DEFERRED_TIMERS_CHECK_NEEDED;	/* assert in fast path of "clear_timers" relies on this */
		}
		if (tp->safe)
			safe_timer_cnt--;
		tp->next = (GT_TIMER *)timefree;	/* place element on free queue */
		timefree = tp;
		num_timers_free++;
		assert(0 < num_timers_free);
		/* assert that no duplicate timer entry with the same "tid" exists in the timer chain */
		assert((NULL == find_timer(tid, &tpp)));
	}
	DUMP_TIMER_INFO("After invoking remove_timer()");
}

/* System call to cancel timer. Not static because can be called from generic_signal_handler() to stop timers
 * from popping yet preserve the blocks so gtmpcat can pick them out of the core. Note that once we exit,
 * timers are cleared at the top of the exit handler.
 */
void sys_canc_timer()
{
#	ifdef BSD_TIMER
	struct itimerval zero;

	memset(&zero, 0, SIZEOF(struct itimerval));
	assert(timer_active);
	/* In case of canceling the system timer, we do not care if we succeed. Consider the two scenarios:
	 *   1) The process is exiting, so all timers must have been removed anyway, and regardless of whether the system
	 *      timer got unset or not, no handlers would be processed (even in the event of a pop).
	 *   2) Some timer is being canceled as part of the runtime logic. If the system is experiencing problems, then the
	 *      following attempt to schedule a new timer (remember that we at the very least have the heartbeat timer once
	 *      database access has been established) would fail; if no other timer is scheduled, then the canceled entry
	 *      must have been removed off the queue anyway, so no processing would occur on a pop.
	 */
	if (-1 == setitimer(ITIMER_REAL, &zero, &old_sys_timer))
	{
		REPORT_SETITIMER_ERROR("ITIMER_REAL", zero, FALSE, errno);
	}
#	else
	alarm(0);
#	endif
	timer_active = FALSE;		/* no timer is active now */
}

/* Cancel all unsafe timers. */
void cancel_unsafe_timers(void)
{
	ABS_TIME	at;
	sigset_t	savemask;
	GT_TIMER	*active, *curr, *next;
	int		rc;
	DEBUG_ONLY(int4	cnt = 0;)
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	DUMP_TIMER_INFO("At the start of cancel_unsafe_timers()");
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_BLOCK, &blockalrm, &savemask, rc);	/* block SIGALRM signal */
	active = curr = (GT_TIMER *)timeroot;
	while (curr)
	{	/* If the timer is unsafe, remove it from the chain. */
		next = curr->next;
		if (!curr->safe)
			remove_timer(curr->tid);
		curr = next;
		DEBUG_ONLY(cnt++;)
	}
	assert((NULL == timeroot) || (0 < safe_timer_cnt));
	if (timeroot)
	{	/* If the head of the queue has changed, or the system timer was not running, start the current first timer. */
		if ((active != timeroot) || (!timer_active))
		{
			sys_get_curr_time(&at);
			start_first_timer(&at);
		}
	} else
	{
		CLEAR_DEFERRED_TIMERS_CHECK_NEEDED;
		/* There are no timers left, but the system timer was active, so cancel it. */
		if (timer_active)
			sys_canc_timer();
	}
#	ifdef DEBUG
	if (WBTEST_ENABLED(WBTEST_DEFERRED_TIMERS))
	{
		DBGFPF((stderr, "CANCEL_ALL_TIMERS:\n"));
		DBGFPF((stderr, " Timer pops handled: %d\n", timer_pop_cnt));
		DBGFPF((stderr, " Timers canceled: %d\n", cnt));
	}
#	endif
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_SETMASK, &savemask, NULL, rc);
	DUMP_TIMER_INFO("After invoking cancel_unsafe_timers()");
}

/* Initialize timers. */
STATICFNDEF void init_timers()
{
	struct sigaction	act;

	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = timer_handler;
	sigaction(SIGALRM, &act, &prev_alrm_handler);
	if (first_timeset && 					/* not from timer_handler to prevent dup message */
	    (SIG_IGN != prev_alrm_handler.sa_handler) &&	/* as set by sig_init */
	    (SIG_DFL != prev_alrm_handler.sa_handler)) 		/* utils, compile */
	{
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_TIMERHANDLER, 3, prev_alrm_handler.sa_handler,
			LEN_AND_LIT("init_timers"));
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_TIMERHANDLER, 3, prev_alrm_handler.sa_handler,
			LEN_AND_LIT("init_timers"));
		assert(FALSE);
	}
}

/* Check for deferred timers. Drive any timers that have been deferred. In case the system timer is
 * disabled, launch it for the next scheduled event. This function should be called upon leaving the
 * interrupt-deferred zone.
 */
void check_for_deferred_timers(void)
{
	sigset_t	savemask;
	int		rc;
	char		*rname;

	assert(!INSIDE_THREADED_CODE(rname));	/* below code is not thread safe as it does SIGPROCMASK() etc. */
	CLEAR_DEFERRED_TIMERS_CHECK_NEEDED;
	timer_handler(DUMMY_SIG_NUM);
}

/* Check for timer pops. If any timers are on the queue, pretend a sigalrm occurred, and we have to
 * check everything. This is mainly for use after external calls until such time as external calls
 * can use this timing facility. Current problem is that external calls are doing their own catching
 * of sigalarms that should be ours, so we end up hung.
 */
void check_for_timer_pops()
{
	int			rc, stolenwhen = 0;		/* 0 = no, 1 = not first, 2 = first time */
	sigset_t 		savemask;
	struct sigaction 	current_sa;

	sigaction(SIGALRM, NULL, &current_sa);	/* get current info */
	if (!first_timeset)
	{
		if (timer_handler != current_sa.sa_handler)	/* check if what we expected */
		{
			init_timers();
			if (!stolen_timer)
			{
				stolen_timer = TRUE;
				stolenwhen = 1;
			}
		}
	} else	/* we haven't set so should be ... */
	{
		if ((SIG_IGN != current_sa.sa_handler) &&	/* as set by sig_init */
		    (SIG_DFL != current_sa.sa_handler)) 	/* utils, compile */
		{
			if (!stolen_timer)
			{
				stolen_timer = TRUE;
				stolenwhen = 2;
			}
		}
	}
	if (timeroot && (1 > timer_stack_count))
	{
		timer_handler(DUMMY_SIG_NUM);
	}
	if (stolenwhen)
	{
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_TIMERHANDLER, 3, current_sa.sa_handler,
			LEN_AND_STR(whenstolen[stolenwhen - 1]));
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_TIMERHANDLER, 3, current_sa.sa_handler,
			LEN_AND_STR(whenstolen[stolenwhen - 1]));
		assert(FALSE);					/* does not return here */
	}
}

/* Externally exposed routine that does a find_timer and is SIGALRM interrupt safe. */
GT_TIMER *find_timer_intr_safe(TID tid, GT_TIMER **tprev)
{
	sigset_t 	savemask;
	GT_TIMER	*tcur;
	int		rc;

	/* Before scanning timer queues, block SIGALRM signal as otherwise that signal could cause an interrupt
	 * timer routine to be driven which could in turn modify the timer queues while this mainline code is
	 * examining the very same queue. This could cause all sorts of invalid returns (of tcur and tprev)
	 * from the find_timer call below.
	 */
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_BLOCK, &blockalrm, &savemask, rc);
	tcur = find_timer(tid, tprev);
	if (1 > timer_stack_count)
		SIGPROCMASK(SIG_SETMASK, &savemask, NULL, rc);
	return tcur;
}
