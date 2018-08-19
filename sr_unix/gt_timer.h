/****************************************************************
 *								*
 * Copyright (c) 2001-2016 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef GT_TIMER_H
#define GT_TIMER_H

/* System dependent include file for gtm timer package */
#include <signal.h>

typedef INTPTR_T TID;		/* Timer ID type */
typedef void (*timer_hndlr)();	/* Timer handler type */

/* Gtm timer package uses ABS_TIME structure to carry
 * the time information in operating system independent
 * manner. The time in this structure is stored as
 * an absolute time - elapsed time since some major historic
 * event, or some other fixed date in the past. Different
 * operating systems have different time reference points.
 *
 * The time is converted from the OS time format to
 * the ABS_TIME format, and from then on, all
 * timer related code uses this format.
 */
typedef struct tag_abs_time
{
#ifndef __osf__
	long	at_sec;		/* seconds */
	long	at_usec;	/* and microseconds */
#else	/* avoid  8 byte alignment issues */
	intszofptr_t	at_sec;		/* seconds */
	intszofptr_t	at_usec;	/* and microseconds */
#endif
} ABS_TIME;

#include <sys/time.h>

/* Type that corresponds to the tv_usec field in a timeval struct.  Valid across all platforms */
#if defined(__linux__) || defined(__ia64) || defined(__sparc) || defined(_AIX) || defined(__MVS__) \
|| defined(__CYGWIN__) || defined(__APPLE__)
    typedef     suseconds_t     gtm_tv_usec_t;
#elif defined(__hppa)
    typedef     long            gtm_tv_usec_t;
#elif defined(__osf__)
    typedef     int             gtm_tv_usec_t;
#elif !defined(VMS)
#   error unsupported platform
#endif

/* All timer requests are placed into a linked list, or
 * a queue of pending requests in a time order.
 * The first timer in this queue is the currently
 * active timer, and expires first.
 */
typedef struct tag_ts
{
	ABS_TIME	expir_time;	/* Absolute time when timer expires */
	ABS_TIME	start_time;	/* Time when the timer is added */
	void		(*handler)();	/* Pointer to handler routine */
	struct tag_ts	*next;		/* Pointer to next */
        TID             tid;            /* Timer id */
        int4            safe;           /* Indicates if handler can be delivered while we are in
					 * a deferred mode
					 */
	int4		block_int;	/* Set to intrpt_state_t value if timer handling was blocked
					 * by an uninterruptible state. For use in debugging, but not
					 * conditional on DEBUG to keep the structure stable.
					 */
	int4		hd_len_max;	/* Max length this blk can hold */
	int4		hd_len;		/* Handler data length */
	char		hd_data[1];	/* Handler data */
} GT_TIMER;

/* Struct to track timefree block allocations */
typedef struct st_timer_alloc
{
	void 			*addr;
	struct st_timer_alloc	*next;
} st_timer_alloc;

#define MAX_SAFE_TIMER_HNDLRS		16	/* Max # of safe timer handlers */
#define GT_WAKE
#define CANCEL_TIMERS			cancel_unsafe_timers()

/* Set a timeout timer, using a local variable as the timeout indicator.
 * Note: This macro establishes a condition handler which cancels the timer in case of an rts_error.
 *       The TIMEOUT_DONE() macro must be used on all normal routine exit paths in order to REVERT the handler.
 */
#define TIMEOUT_INIT(TIMEOUT_VAR, TIMEOUT_MILLISECS)									\
MBSTART {														\
	boolean_t	*ptr_to_##TIMEOUT_VAR = &(TIMEOUT_VAR);								\
	boolean_t	got_error;											\
															\
	TIMEOUT_VAR = FALSE;												\
	start_timer((TID)ptr_to_##TIMEOUT_VAR, TIMEOUT_MILLISECS, simple_timeout_timer,					\
			SIZEOF(ptr_to_##TIMEOUT_VAR), &ptr_to_##TIMEOUT_VAR);						\
	ESTABLISH_NORET(timer_cancel_ch, got_error);									\
	if (got_error)													\
	{														\
		TIMEOUT_DONE(TIMEOUT_VAR);										\
		DRIVECH(error_condition);										\
	}														\
} MBEND
#define TIMEOUT_DONE(TIMEOUT_VAR)						\
MBSTART {									\
	boolean_t	*ptr_to_##TIMEOUT_VAR = &(TIMEOUT_VAR);			\
										\
	assert(ctxt->ch == timer_cancel_ch);					\
	REVERT;									\
	cancel_timer((TID)ptr_to_##TIMEOUT_VAR);				\
} MBEND

/* Lighter versions with no condition handler, for when it is safe to have the timer hang around after it is no longer relevant. */
#define TIMEOUT_INIT_NOCH(TIMEOUT_VAR, TIMEOUT_MILLISECS)								\
MBSTART {														\
	boolean_t	*ptr_to_##TIMEOUT_VAR = &(TIMEOUT_VAR);								\
															\
	TIMEOUT_VAR = FALSE;												\
	start_timer((TID)ptr_to_##TIMEOUT_VAR, TIMEOUT_MILLISECS, simple_timeout_timer,					\
			SIZEOF(ptr_to_##TIMEOUT_VAR), &ptr_to_##TIMEOUT_VAR);						\
} MBEND
#define TIMEOUT_DONE_NOCH(TIMEOUT_VAR)						\
MBSTART {									\
	boolean_t	*ptr_to_##TIMEOUT_VAR = &(TIMEOUT_VAR);			\
										\
	cancel_timer((TID)ptr_to_##TIMEOUT_VAR);				\
} MBEND

/* Uncomment the below #define if you want to print the status of the key timer-related variables as well as the entire timer queue
 * when operations such as addition, cancellation, or handling of a timer occur.
 *
 * #define TIMER_DEBUGGING
 */

#ifdef TIMER_DEBUGGING
#  define DUMP_TIMER_INFO(LOCATION)								\
{												\
	extern void	(*fake_enospc_ptr)();							\
	extern void	(*simple_timeout_timer_ptr)();						\
	int		i;									\
	GT_TIMER	*cur_timer;								\
	char		*s_jnl_file_close_timer = "jnl_file_close_timer";			\
	char		*s_wcs_clean_dbsync = "wcs_clean_dbsync";				\
	char		*s_wcs_stale = "wcs_stale";						\
	char		*s_hiber_wake = "hiber_wake";						\
	char		*s_fake_enospc = "fake_enospc";						\
	char		*s_simple_timeout_timer = "simple_timeout_timer";			\
	char		s_unknown[20];								\
	char		*handler;								\
												\
	cur_timer = (GT_TIMER *)timeroot;							\
	FPRINTF(stderr, "------------------------------------------------------\n"		\
		"%s\n---------------------------------\n"					\
		"Timer Info:\n"									\
		"  system timer active: %d\n"							\
		"  timer in handler:    %d\n"							\
		"  timer stack count:   %d\n"							\
		"  oldjnlclose started: %d\n",							\
		LOCATION, timer_active, timer_in_handler,					\
		timer_stack_count, oldjnlclose_started);					\
	FFLUSH(stderr);										\
	i = 0;											\
	while (cur_timer)									\
	{											\
		if ((void (*)())jnl_file_close_timer_ptr == cur_timer->handler)			\
			handler = s_jnl_file_close_timer;					\
		else if ((void (*)())wcs_clean_dbsync_fptr == cur_timer->handler)		\
			handler = s_wcs_clean_dbsync;						\
		else if ((void (*)())wcs_stale_fptr == cur_timer->handler)			\
			handler = s_wcs_stale;							\
		else if ((void (*)())hiber_wake == cur_timer->handler)				\
			handler = s_hiber_wake;							\
		else if ((void (*)())fake_enospc_ptr == cur_timer->handler)			\
			handler = s_fake_enospc;						\
		else if ((void (*)())simple_timeout_timer_ptr == cur_timer->handler)		\
			handler = s_simple_timeout_timer;					\
		else										\
		{										\
			SPRINTF(s_unknown, "%p", (void *)handler);				\
			handler = s_unknown;							\
		}										\
		FPRINTF(stderr, "  - timer #%d:\n"						\
			"      handler:    %s\n"						\
			"      safe:       %d\n"						\
			"      start_time: [at_sec: %ld; at_usec: %ld]\n"			\
			"      expir_time: [at_sec: %ld; at_usec: %ld]\n",			\
			i, handler, cur_timer->safe,						\
			cur_timer->start_time.at_sec, cur_timer->start_time.at_usec,		\
			cur_timer->expir_time.at_sec, cur_timer->expir_time.at_usec);		\
		FFLUSH(stderr);									\
		cur_timer = cur_timer->next;							\
		i++;										\
	}											\
	FPRINTF(stderr, "------------------------------------------------------\n");		\
	FFLUSH(stderr);										\
}
#else
#  define DUMP_TIMER_INFO(LOCATION)
#endif

int4		abs_time_comp(ABS_TIME *atp1, ABS_TIME *atp2);
void		add_int_to_abs_time(ABS_TIME *atps, int4 ival, ABS_TIME *atpd);
void		cancel_timer(TID tid);
void		cancel_unsafe_timers(void);
void		clear_timers(void);
void		hiber_start(uint4 hiber);
void		hiber_start_wait_any(uint4 hiber);
void		gtm_start_timer(TID tid, int4 time_to_expir, void(* handler)(), int4 data_length, void *handler_data);
void		start_timer(TID tid, int4 time_to_expir, void(* handler)(), int4 data_length, void *handler_data);
ABS_TIME	sub_abs_time(ABS_TIME *atp1, ABS_TIME *atp2);
void		sys_get_curr_time(ABS_TIME *atp);
void		prealloc_gt_timers(void);
void		set_blocksig(void);
void		check_for_timer_pops(void);
GT_TIMER	*find_timer_intr_safe(TID tid, GT_TIMER **tprev);
void		check_for_deferred_timers(void);
void		add_safe_timer_handler(int safetmr_cnt, ...);
void		sys_canc_timer(void);
void 		simple_timeout_timer(TID tid, int4 hd_len, boolean_t **timedout);

STATICFNDCL void	hiber_wake(TID tid, int4 hd_len, int4 **waitover_flag);
STATICFNDCL void	gt_timers_alloc(void);
STATICFNDCL void	start_timer_int(TID tid, int4 time_to_expir, void (*handler)(), int4 hdata_len,
					void *hdata, boolean_t safe_timer);
STATICFNDCL void	sys_settimer (TID tid, ABS_TIME *time_to_expir);
STATICFNDCL void	start_first_timer(ABS_TIME *curr_time);
STATICFNDCL void	timer_handler(int why);
STATICFNDCL GT_TIMER	*find_timer(TID tid, GT_TIMER **tprev);
STATICFNDCL GT_TIMER	*add_timer(ABS_TIME *atp, TID tid, int4 time_to_expir, void (*handler)(), int4 hdata_len,
					void *hdata, boolean_t safe_timer);
STATICFNDCL void	remove_timer(TID tid);
STATICFNDCL void	init_timers(void);

#endif
