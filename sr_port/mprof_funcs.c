/****************************************************************
 *								*
 * Copyright (c) 2001-2017 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_stdio.h"
#include <errno.h>
#include "gtm_fcntl.h"
#include "gtm_signal.h"
#include "gtm_stat.h"
#include "gtm_unistd.h"
#include "min_max.h"
#include "gtm_time.h"

/* OSE/SMH - Extra defs for Macs (which are also UNIX) */
#ifdef __APPLE__
/*
http://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x
https://developer.apple.com/library/mac/qa/qa1398/_index.html
*/
#include <mach/mach_time.h> /* mach_absolute_time */
#include <mach/mach.h>      /* host_get_clock_service, mach_... */
#include <mach/clock.h>     /* clock_get_time */

#define BILLION 1000000000L
#define MILLION 1000000L
#endif


#include "gtm_ctype.h"
#include "gtm_string.h"
#include "error.h"
#include <rtnhdr.h>
#include "stack_frame.h"
#include "subscript.h"
#include "svnames.h"
#include "mprof.h"
#include "outofband.h"
#include "op.h"
#include "lv_val.h"		/* Needed for callg.h. */
#include "callg.h"
#include "gtmmsg.h"
#include "str2gvargs.h"

GBLREF 	boolean_t		is_tracing_on;
GBLREF 	stack_frame		*frame_pointer;
GBLREF 	mval			dollar_job;
GBLREF	uint4			process_id;
GBLREF	int * volatile		var_on_cstack_ptr;		/* Volatile so that nothing gets optimized out. */
GBLREF	int4			gtm_trigger_depth;

STATICDEF boolean_t 		save_to_gbl = TRUE;		/* Indicates whether profiling info is to be saved to db. */
STATICDEF boolean_t 		mdb_ch_set;			/* Indicates whether we can rely on mdb_condition_handler and issue
								 * an rts_error. */
STATICDEF gtm_uint64_t		child_system, child_user;	/* Store system and user CPU time for child processes. */
STATICDEF gtm_uint64_t		process_system, process_user;	/* Store system and user CPU time for current process. */
STATICDEF mstr			mprof_mstr;			/* Area to hold global and subscripts. */
STATICDEF boolean_t 		use_realtime_flag = FALSE;	/* Indicates whether clock_gettime is unable to use CLOCK_MONOTONIC
								 * flag and so should use CLOCK_REALTIME instead. */

LITDEF  MIDENT_CONST(above_routine, "*above*");

#ifdef DEBUG
#  define RUNTIME_LIMIT		604800000000.0	/* Not a long because on 32-bit platforms longs are only 4 bytes. */
#endif

#define CHILDREN_TIME		"*CHILDREN"	/* Label to store CPU time for child processes. */
#define PROCESS_TIME		"*RUN"		/* Label to store CPU time for current process. */
#define MPROF_NULL_LABEL	"^"
#define MPROF_FOR_LOOP		"FOR_LOOP"
#define TIMES			times_usec
#define CHILDREN_TIMES		children_times_usec
#define MPROF_RTS_ERROR(x)	rts_error_csa x

#define UPDATE_TIME(MPP)												\
{															\
	mprof_wrapper *mpp = (MPP);											\
															\
	if (mpp->tcurr.tms_utime > mpp->tprev.tms_utime)								\
		mpp->curr_tblnd->e.usr_time += (mpp->tcurr.tms_utime - mpp->tprev.tms_utime);				\
	if (mpp->tcurr.tms_stime > mpp->tprev.tms_stime)								\
		mpp->curr_tblnd->e.sys_time += (mpp->tcurr.tms_stime - mpp->tprev.tms_stime);				\
	if (mpp->tcurr.tms_etime > mpp->tprev.tms_etime)								\
		mpp->curr_tblnd->e.elp_time += (mpp->tcurr.tms_etime - mpp->tprev.tms_etime);				\
	/* It should be a reasonable assumption that in debug no M process will use more than a week of either user,	\
	 * system, or even absolute runtime.										\
	 */														\
	assert((mpp->curr_tblnd->e.usr_time < RUNTIME_LIMIT) && (mpp->curr_tblnd->e.sys_time < RUNTIME_LIMIT)		\
			&& (mpp->curr_tblnd->e.elp_time < RUNTIME_LIMIT));						\
}

#define RTS_ERROR_VIEWNOTFOUND(x)	MPROF_RTS_ERROR((CSA_ARG(NULL) VARLSTCNT(8) ERR_VIEWNOTFOUND, 2, gvn->str.len,	\
						gvn->str.addr, ERR_TEXT, 2, RTS_ERROR_STRING(x)));


/* do the MPROF initialization */
#define INIT_PROF_FP					\
	TREF(prof_fp) = mprof_stack_push();		\
	(TREF(prof_fp))->prev = NULL;			\
	(TREF(prof_fp))->curr_node = NULL;		\
	(TREF(prof_fp))->start.tms_stime = 0;		\
	(TREF(prof_fp))->start.tms_utime = 0;		\
	(TREF(prof_fp))->start.tms_etime = 0;		\
	(TREF(prof_fp))->carryover.tms_stime = 0;	\
	(TREF(prof_fp))->carryover.tms_utime = 0;	\
	(TREF(prof_fp))->carryover.tms_etime = 0;	\
	(TREF(prof_fp))->dummy_stack_count = 0;		\
	(TREF(prof_fp))->rout_name = NULL;		\
	(TREF(prof_fp))->label_name = NULL;

/* Monotonic flag for clock_gettime() is defined differently on every platform. */
#ifndef CLOCK_MONOTONIC
#  ifdef __sparc
#    define CLOCK_MONOTONIC CLOCK_HIGHRES
#  else
#    define CLOCK_MONOTONIC CLOCK_REALTIME
#  endif
#endif

error_def(ERR_MAXNRSUBSCRIPTS);
error_def(ERR_NOTGBL);
error_def(ERR_STRUNXEOR);
error_def(ERR_SYSCALL);
error_def(ERR_TEXT);
error_def(ERR_TRACINGON);
error_def(ERR_VIEWNOTFOUND);

STATICFNDEF void times_usec(ext_tms *curr)
{
	int		res;
	struct rusage	usage;
	struct timespec	elp_time;
#ifdef __APPLE__
	static mach_timebase_info_data_t timebase = { 0, 0 };
#endif

	res = getrusage(RUSAGE_SELF, &usage);
	if (-1 == res)
		MPROF_RTS_ERROR((CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5, LEN_AND_LIT("getrusage"), CALLFROM, errno));
	curr->tms_utime = (usage.ru_utime.tv_sec * (gtm_uint64_t)1000000) + usage.ru_utime.tv_usec;
	curr->tms_stime = (usage.ru_stime.tv_sec * (gtm_uint64_t)1000000) + usage.ru_stime.tv_usec;
	/* Also start recording the elapsed time. */
#ifdef __APPLE__
	if (mach_timebase_info(&timebase) != 0)
				MPROF_RTS_ERROR((CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5,
					LEN_AND_LIT("mach_timebase_info"), CALLFROM, errno));

	gtm_uint64_t mac_clock = mach_absolute_time();
	gtm_uint64_t nano = mac_clock * (gtm_uint64_t)timebase.numer / (gtm_uint64_t)timebase.denom;
	elp_time.tv_sec  = nano / BILLION;
	elp_time.tv_nsec = nano % BILLION; 
	/* DEBUG */
	/* fprintf(stderr,"%ld %ld",elp_time.tv_sec, elp_time.tv_nsec); */
	/* DEBUG */
#else
	while (TRUE)
	{
		res = clock_gettime(use_realtime_flag ? CLOCK_REALTIME : CLOCK_MONOTONIC, &elp_time);
		if (res == -1)
		{
			if ((EINVAL == errno) && !use_realtime_flag)
			{
				use_realtime_flag = TRUE;
				continue;
			} else
				MPROF_RTS_ERROR((CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5,
					LEN_AND_LIT("clock_gettime"), CALLFROM, errno));
		}
		break;
	}
#endif
	curr->tms_etime = (elp_time.tv_sec * (gtm_uint64_t)1000000) + (elp_time.tv_nsec / 1000);
	return;
}

STATICFNDEF void child_times_usec(void)
{
	int		res;
	struct rusage	usage;

	res = getrusage(RUSAGE_CHILDREN, &usage);
	if (res == -1)
		MPROF_RTS_ERROR((CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5, LEN_AND_LIT("getrusage"), CALLFROM, errno));
	child_user = (usage.ru_utime.tv_sec * (gtm_uint64_t)1000000) + usage.ru_utime.tv_usec;
	child_system = (usage.ru_stime.tv_sec * (gtm_uint64_t)1000000) + usage.ru_stime.tv_usec;
	return;
}

/* Enables tracing and ensures that all critical structures are initialized. */
void turn_tracing_on(mval *gvn, boolean_t from_env, boolean_t save_gbl)
{
	trace_entry	tmp_trc_tbl_entry;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	mdb_ch_set = !from_env;
	/* If tracing is on explicitly, or if it is implicit with saving. */
	if (save_gbl || !from_env)
	{
		if (is_tracing_on)
		{
			gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_TRACINGON);
			return;
		}
		if ((0 == gvn->str.len) || ('^' != gvn->str.addr[0]))
			MPROF_RTS_ERROR((CSA_ARG(NULL) VARLSTCNT(4) ERR_NOTGBL, 2, gvn->str.len, gvn->str.addr));
	}
	/* The following should only be a one-time operation. */
	if (!TREF(mprof_ptr))
	{
		TREF(mprof_ptr) = (mprof_wrapper *)malloc(SIZEOF(mprof_wrapper));
		memset(TREF(mprof_ptr), 0, SIZEOF(mprof_wrapper));
	}
	/* Only need to have the gvn if we are going to save the data. */
	if (save_gbl && (0 < gvn->str.len))
	{
		parse_gvn(gvn);
		(TREF(mprof_ptr))->gbl_to_fill = *gvn;
		(TREF(mprof_ptr))->gbl_to_fill.str.addr = (char *)malloc(gvn->str.len); /* Since len was already set up. */
		memcpy((TREF(mprof_ptr))->gbl_to_fill.str.addr, gvn->str.addr, gvn->str.len);
	}
	/* Preallocate some space. */
	if (!(TREF(mprof_ptr))->pcavailbase)
	{
		(TREF(mprof_ptr))->pcavailbase = (char **)malloc(PROFCALLOC_DSBLKSIZE);
		*(TREF(mprof_ptr))->pcavailbase = 0;
	}
	(TREF(mprof_ptr))->pcavailptr = (TREF(mprof_ptr))->pcavailbase;
	(TREF(mprof_ptr))->pcavail = PROFCALLOC_DSBLKSIZE - OFFSET_LEN;
	memset((TREF(mprof_ptr))->pcavailptr + 1, 0, (TREF(mprof_ptr))->pcavail);
	TIMES(&(TREF(mprof_ptr))->tprev);
	UNIX_ONLY(child_times_usec();)
	mprof_stack_init();
	/* If tracing is started explicitly, we are in a good frame, so we can initialize things and start counting time. */
	if (!from_env)
	{
		TREF(prof_fp) = mprof_stack_push();
		get_entryref_information(FALSE, NULL);
		tmp_trc_tbl_entry.rout_name = NULL;
		tmp_trc_tbl_entry.label_name = NULL;
		(TREF(mprof_ptr))->curr_tblnd = (TREF(mprof_ptr))->head_tblnd = NULL;
		(TREF(prof_fp))->start.tms_stime = (TREF(mprof_ptr))->tprev.tms_stime;
		(TREF(prof_fp))->start.tms_utime = (TREF(mprof_ptr))->tprev.tms_utime;
		(TREF(prof_fp))->start.tms_etime = (TREF(mprof_ptr))->tprev.tms_etime;
		(TREF(prof_fp))->carryover.tms_stime = 0;
		(TREF(prof_fp))->carryover.tms_utime = 0;
		(TREF(prof_fp))->carryover.tms_etime = 0;
		(TREF(prof_fp))->dummy_stack_count = 0;
		(TREF(prof_fp))->rout_name = (TREF(prof_fp))->label_name = NULL;
	}
	/* Make necessary xfer_table substitutions before we begin. */
	if (!is_tracing_on)
	{
		POPULATE_PROFILING_TABLE();
		is_tracing_on = TRUE;
	}
	/* Remember if we need to save results to a global at the end. */
	if (!save_gbl)
		save_to_gbl = FALSE;
	mdb_ch_set = TRUE;
	return;
}

/* Disables tracing and properly disposes of allocated resources; additionally,
 * saves data to the database if save_to_gbl was set to TRUE.
 */
void turn_tracing_off(mval *gvn)
{
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (FALSE == is_tracing_on)
		return;
	assert(TREF(mprof_ptr));
	TIMES(&(TREF(mprof_ptr))->tcurr);
	UNIX_ONLY(child_times_usec();)
	process_system = (TREF(mprof_ptr))->tcurr.tms_stime;
	process_user = (TREF(mprof_ptr))->tcurr.tms_utime;
	/* Update the time of previous M line if there was one. */
	if (NULL != (TREF(mprof_ptr))->curr_tblnd)
	{
		UPDATE_TIME(TREF(mprof_ptr));
	}
	if (NULL != gvn)
		parse_gvn(gvn);
	assert(!save_to_gbl || (0 != (TREF(mprof_ptr))->gbl_to_fill.str.addr));
	/* If tracing was initialized from an environment variable, and it had a proper global name, save results
	 * to that global; otherwise, just toss the collected data.
	 */
	if (save_to_gbl)
	{
		if (NULL != (TREF(mprof_ptr))->head_tblnd)
		{
			UNIX_ONLY(insert_total_times(FALSE);)
			insert_total_times(TRUE);
			mprof_tree_walk((TREF(mprof_ptr))->head_tblnd);
		}
		free((TREF(mprof_ptr))->gbl_to_fill.str.addr);
		(TREF(mprof_ptr))->gbl_to_fill.str.addr = NULL;
	}
	is_tracing_on = (TREF(mprof_ptr))->is_tracing_ini = FALSE;
	mprof_stack_free();
	(TREF(mprof_ptr))->pcavailptr = (TREF(mprof_ptr))->pcavailbase;
	(TREF(mprof_ptr))->pcavail = PROFCALLOC_DSBLKSIZE - OFFSET_LEN;
	CLEAR_PROFILING_TABLE();	/* Restore the original xfer_table links. */
	TREF(prof_fp) = NULL;
	return;
}

/* Takes care of properly embedding FOR count and nesting information into MPROF
 * tree nodes. Called in the beginning of any FOR loop iteration.
 */
void forchkhandler(char *return_address)
{
        trace_entry     	tmp_trc_tbl_entry;
	int			for_level_on_line;
	mprof_tree		*for_link, *for_node;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
        get_entryref_information(TRUE, &tmp_trc_tbl_entry);
	/* Starting tracing now; save the info about the current FOR loop. */
	if (FALSE == (TREF(mprof_ptr))->is_tracing_ini)
	{
		(TREF(mprof_ptr))->is_tracing_ini = TRUE;
		(TREF(mprof_ptr))->curr_tblnd = (TREF(mprof_ptr))->head_tblnd = (mprof_tree *)new_node(&tmp_trc_tbl_entry);
		(TREF(mprof_ptr))->curr_tblnd->e.count = 1;
		for_node = (mprof_tree *)new_for_node(&tmp_trc_tbl_entry, return_address);
		for_node->e.count = 1;
		for_node->e.loop_level = 1;
		(TREF(mprof_ptr))->curr_tblnd->loop_link = (mprof_tree *)for_node;
		return;
	}
	(TREF(mprof_ptr))->curr_tblnd =
		(mprof_tree *)mprof_tree_insert(&((TREF(mprof_ptr))->head_tblnd), &tmp_trc_tbl_entry);
	if (NULL == (TREF(mprof_ptr))->curr_tblnd->loop_link)
	{	/* First FOR for this node. */
		for_node = (mprof_tree *)new_for_node(&tmp_trc_tbl_entry, return_address);
		for_node->e.count = 1;
		for_node->e.loop_level = 1;
		(TREF(mprof_ptr))->curr_tblnd->loop_link = (mprof_tree *)for_node;
		return;
	}
	/* Some FORs have been already recorded for this line, so keep checking for more. */
	for_link = (mprof_tree *)(TREF(mprof_ptr))->curr_tblnd->loop_link;
	for_level_on_line = 1;
	while (TRUE)
	{	/* Same FOR, so just update the count. */
		if (for_link->e.raddr == return_address)
		{
			for_link->e.count++;
			break;
		}
		/* New FOR for this line. */
		if (NULL == for_link->loop_link)
		{
			for_node = (mprof_tree *)new_for_node(&tmp_trc_tbl_entry, return_address);
			for_node->e.count = 1;
			for_node->e.loop_level = for_level_on_line + 1;
			for_link->loop_link = (mprof_tree *)for_node;
			break;
		} else
		{	/* Same FOR as last one, so increase the loop level for the line and keep searching. */
			for_link = (mprof_tree *)for_link->loop_link;
			for_level_on_line++;
		}
	}
	return;
}

/* Records the typical line-to-line deltas in CPU and absolute time. Called on each linestart and linefetch. */
void pcurrpos(void)
{
	trace_entry	tmp_trc_tbl_entry;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (NULL == TREF(prof_fp))
	{
		INIT_PROF_FP;
	}
	assert(TREF(mprof_ptr));
	assert(TREF(prof_fp));
	TIMES(&(TREF(mprof_ptr))->tcurr); /* Remember the new time. */
	/* Update the time of previous M line. */
	if (NULL != (TREF(mprof_ptr))->curr_tblnd)
	{
		UPDATE_TIME(TREF(mprof_ptr));
	}
	get_entryref_information(TRUE, &tmp_trc_tbl_entry);
	if (FALSE == (TREF(mprof_ptr))->is_tracing_ini)
	{
		(TREF(mprof_ptr))->is_tracing_ini = TRUE;
		(TREF(mprof_ptr))->curr_tblnd = (TREF(mprof_ptr))->head_tblnd = (mprof_tree *)new_node(&tmp_trc_tbl_entry);
		(TREF(mprof_ptr))->curr_tblnd->e.count = 1;
	} else
	{
		(TREF(mprof_ptr))->curr_tblnd =
			(mprof_tree *)mprof_tree_insert(&((TREF(mprof_ptr))->head_tblnd), &tmp_trc_tbl_entry);
		if (0 == (TREF(mprof_ptr))->curr_tblnd->e.count)
			(TREF(mprof_ptr))->curr_tblnd->e.count = 1;
		else
			(TREF(mprof_ptr))->curr_tblnd->e.count++;
	}
	(TREF(mprof_ptr))->tprev = (TREF(mprof_ptr))->tcurr;
	return;
}

/* Called upon entering a new label. The time account for a label excludes the time spent within any other label
 * called from it. An example:
 *
 *	d l1	|			t0
 *		|
 *		|
 *		| d l2	|		t1
 *			|
 *			|
 *			|
 *			| q		t2
 *		|
 *		|
 *		|
 *		|
 *		| q			t3
 *
 *	Results should be:
 *		l1 label:
 *			(t3 - t2) + (t1 - t0)
 *
 *		l2 label:
 *			(t2 - t1)
 */
void new_prof_frame(int real_frame)
{
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (NULL == TREF(prof_fp))
	{
		INIT_PROF_FP;
	}
	assert(TREF(mprof_ptr));
	assert(TREF(prof_fp));
	/* A call to a routine, not IF or FOR with a DO. */
	if (real_frame)
	{	/* Update the time of the line in the parent frame. */
		if (NULL != (TREF(mprof_ptr))->curr_tblnd)
		{
			TIMES(&(TREF(mprof_ptr))->tcurr);
			UPDATE_TIME(TREF(mprof_ptr));
		}
		(TREF(prof_fp))->curr_node = (TREF(mprof_ptr))->curr_tblnd;
		(TREF(mprof_ptr))->curr_tblnd = NULL;
		/* Create a new frame on the stack. */
		TREF(prof_fp) = mprof_stack_push();
		(TREF(prof_fp))->rout_name = NULL;
		(TREF(prof_fp))->label_name = NULL;
		(TREF(prof_fp))->start = (TREF(mprof_ptr))->tprev = (TREF(mprof_ptr))->tcurr;
		(TREF(prof_fp))->carryover.tms_utime = 0;
		(TREF(prof_fp))->carryover.tms_stime = 0;
		(TREF(prof_fp))->carryover.tms_etime = 0;
		(TREF(prof_fp))->dummy_stack_count = 0;
	} else
		(TREF(prof_fp))->dummy_stack_count++;
	return;
}

/* Records profiling information about the current line and/or label and, if necessary, unwinds
 * frames off the MPROF stack. Called before leaving a label.
 */
void unw_prof_frame(void)
{
	trace_entry	e;
	trace_entry	tmp_trc_tbl_entry;
	mprof_tree	*t;
	ext_tms		carryover;
	stack_frame	*save_fp;
	gtm_uint64_t	frame_usr_time, frame_sys_time, frame_elp_time;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	assert(TREF(mprof_ptr));
	/* Do not assert on prof_fp not being NULL, as it will be NULL in
	 * case we quit from a function without ever disabling tracing.
	 */
	if (NULL == TREF(prof_fp))
		return;
	/* We are leaving a real frame, not an IF or FOR with a DO. */
	if (0 >= (TREF(prof_fp))->dummy_stack_count)
	{
		TIMES(&(TREF(mprof_ptr))->tcurr);
		/* Update the time of last line in this frame before returning. */
		if (NULL != (TREF(mprof_ptr))->curr_tblnd)
		{
			UPDATE_TIME(TREF(mprof_ptr));
		}
		get_entryref_information(TRUE, &tmp_trc_tbl_entry);
		/* If prof_fp is NULL, it was set so in get_entryref_information, which means
		 * that we are not in a valid frame, so there is no point in recording timing for
		 * some unreal label; besides, the line timing has already been updated.
		 */
		if (NULL == TREF(prof_fp))
			return;
		if ((TREF(prof_fp))->rout_name == &above_routine)
		{	/* It should have been filled in get_entryref_information. */
			e.label_name = tmp_trc_tbl_entry.label_name;
			e.rout_name = tmp_trc_tbl_entry.rout_name;
		} else
		{	/* Note that this memory allocated for the label and routine names might need
			 * to be reclaimed, so set up the corresponding flag, reset the allocation count,
			 * and the address of the current allocation bucket.
			 */
			TREF(mprof_alloc_reclaim) = TRUE;
			TREF(mprof_reclaim_addr) = (char *)(TREF(mprof_ptr))->pcavailptr;
			TREF(mprof_reclaim_cnt) = 0;
			e.label_name = (mident *)pcalloc(SIZEOF(mident));
			e.label_name->len = (TREF(prof_fp))->label_name->len;
			e.label_name->addr = pcalloc((unsigned int)e.label_name->len);
			memcpy(e.label_name->addr, (TREF(prof_fp))->label_name->addr, (TREF(prof_fp))->label_name->len);
			e.rout_name = (mident *)pcalloc(SIZEOF(mident));
			e.rout_name->len = (TREF(prof_fp))->rout_name->len;
			e.rout_name->addr = pcalloc((unsigned int)e.rout_name->len);
			memcpy(e.rout_name->addr, (TREF(prof_fp))->rout_name->addr, (TREF(prof_fp))->rout_name->len);
			TREF(mprof_alloc_reclaim) = FALSE;	/* Memory should not have to be reclaimed after this point,
							 	 * so stop updating the count. */
		}
		e.line_num = -1;
		/* Insert/find a frame entry into/in the MPROF tree, -1 indicating that it is not a
		 * real line number, but rather an aggregation of several lines (comprising a label).
		 */
		t = mprof_tree_insert(&((TREF(mprof_ptr))->head_tblnd), &e);
		TREF(mprof_reclaim_cnt) = 0;	/* Reset the memory allocation count. */
		/* Update count and timing (from prof_fp) of frame I'm leaving. */
		t->e.count++;
		frame_usr_time = ((TREF(mprof_ptr))->tcurr.tms_utime
			- (TREF(prof_fp))->start.tms_utime - (TREF(prof_fp))->carryover.tms_utime);
		frame_sys_time = ((TREF(mprof_ptr))->tcurr.tms_stime
			- (TREF(prof_fp))->start.tms_stime - (TREF(prof_fp))->carryover.tms_stime);
		frame_elp_time = ((TREF(mprof_ptr))->tcurr.tms_etime
			- (TREF(prof_fp))->start.tms_etime - (TREF(prof_fp))->carryover.tms_etime);
		t->e.usr_time += frame_usr_time;
		t->e.sys_time += frame_sys_time;
		t->e.elp_time += frame_elp_time;
		/* Not the first frame on the stack. */
		if ((TREF(prof_fp))->prev)
		{
			carryover = (TREF(prof_fp))->carryover;
			/* Move back up to parent frame. */
			TREF(prof_fp) = mprof_stack_pop();
			(TREF(prof_fp))->carryover.tms_utime += (frame_usr_time + carryover.tms_utime);
			(TREF(prof_fp))->carryover.tms_stime += (frame_sys_time + carryover.tms_stime);
			(TREF(prof_fp))->carryover.tms_etime += (frame_elp_time + carryover.tms_etime);
			/* Restore the context of the parent frame. */
			(TREF(mprof_ptr))->tprev = (TREF(mprof_ptr))->tcurr;
			(TREF(mprof_ptr))->curr_tblnd = (TREF(prof_fp))->curr_node;
			(TREF(prof_fp))->curr_node = NULL;
		} else
		{	/* This should only be true only if the VIEW command is not at the top-most stack level, in which case
			 * add profiling information for the current frame. To prevent stack underflow, add a new frame before
			 * unwinding from this frame.
			 */
			mprof_stack_pop();
			(TREF(mprof_ptr))->tprev = (TREF(mprof_ptr))->tcurr;
			(TREF(mprof_ptr))->curr_tblnd = NULL;
			TREF(prof_fp) = mprof_stack_push();
			save_fp = frame_pointer;
#			ifdef GTM_TRIGGER
			if (frame_pointer->type & SFT_TRIGR)
			{	/* In a trigger base frame, old_frame_pointer is NULL. */
				assert(NULL == frame_pointer->old_frame_pointer);
				/* Have a trigger baseframe, pick up stack continuation frame_pointer stored by base_frame(). */
				frame_pointer = *(stack_frame **)(frame_pointer + 1);
			} else
#			endif
			frame_pointer = frame_pointer->old_frame_pointer;
			/* If frame_pointer is NULL, we are likely dealing with call-ins, so there is no point trying to unwind
			 * any further; just restore the frame_pointer and return.
			 */
			if (!frame_pointer)
			{
				frame_pointer = save_fp;
				return;
			}
			get_entryref_information(FALSE, NULL);
			frame_pointer = save_fp;
			/* If for some reason we made it all the way here, but prof_fp was still set to NULL by
			 * get_entryref_information(), then just silently quit out of this routine to prevent unwinding into some
			 * nowhere land.
			 */
			if (NULL == TREF(prof_fp))
				return;
			(TREF(prof_fp))->start = (TREF(mprof_ptr))->tcurr;
			(TREF(prof_fp))->carryover.tms_utime = 0;
			(TREF(prof_fp))->carryover.tms_stime = 0;
			(TREF(prof_fp))->carryover.tms_etime = 0;
			/* Tag it, so that next time it picks up label/routine info from current loc. */
			(TREF(prof_fp))->rout_name = (mident *)&above_routine;
			(TREF(prof_fp))->label_name = NULL;
			(TREF(prof_fp))->dummy_stack_count = 0;
		}
	} else
		(TREF(prof_fp))->dummy_stack_count--;
	return;
}

/* Allocate storage for profiling information. */
char *pcalloc(unsigned int n)
{
	char **x;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
#	ifdef GTM64
	n = ((n + 7) & ~7); 	/* Same logic applied for alignment. */
#	else
	n = ((n + 3) & ~3); 	/* Make sure that it is quad-word aligned. */
#	endif
	if (n > (TREF(mprof_ptr))->pcavail)
	{
		if (*(TREF(mprof_ptr))->pcavailptr)
			(TREF(mprof_ptr))->pcavailptr = (char ** )*(TREF(mprof_ptr))->pcavailptr;
		else
		{
			x = (char **)malloc(PROFCALLOC_DSBLKSIZE);
			*(TREF(mprof_ptr))->pcavailptr = (char *)x;
			(TREF(mprof_ptr))->pcavailptr = x;
			*(TREF(mprof_ptr))->pcavailptr = NULL;
		}
		(TREF(mprof_ptr))->pcavail = PROFCALLOC_DSBLKSIZE - OFFSET_LEN;
		memset((TREF(mprof_ptr))->pcavailptr + 1, 0, (TREF(mprof_ptr))->pcavail);
	}
	(TREF(mprof_ptr))->pcavail -= n;
	if (TREF(mprof_alloc_reclaim))
		(TREF(mprof_reclaim_cnt)) += n;	/* Update the memory allocation count if needed. */
	assert((TREF(mprof_ptr))->pcavail >= 0);
	return (char *)(TREF(mprof_ptr))->pcavailptr + (TREF(mprof_ptr))->pcavail + OFFSET_LEN;
}

/* Reclaim storage previously allocated by pcalloc(). */
void mprof_reclaim_slots(void)
{
	int alloc_diff;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (0 < TREF(mprof_reclaim_cnt))	/* In case some memory needs to be reclaimed, do so. */
	{
		alloc_diff = (PROFCALLOC_DSBLKSIZE - (TREF(mprof_ptr))->pcavail - OFFSET_LEN);
		if (alloc_diff >= TREF(mprof_reclaim_cnt)) /* Allocation did not need a new bucket, we are good. */
			(TREF(mprof_ptr))->pcavail += TREF(mprof_reclaim_cnt);
		else
		{	/* Go back to the previous allocation bucket and set the pcavail accordingly for the older bucket. */
			(TREF(mprof_ptr))->pcavailptr = (char **)TREF(mprof_reclaim_addr);
			(TREF(mprof_ptr))->pcavail = (TREF(mprof_reclaim_cnt) - alloc_diff);
		}
	}
}

/* Writes the data into the global. */
void crt_gbl(mprof_tree *p, boolean_t is_for)
{
	char		*c_top, *c_ref, ch;
	int		count, arg_index, subsc_len, tmp_str_len;
	INTPTR_T	start_point;
	mval		data;
	char		dataval[96];		/* Big enough for data value. */
	unsigned char	subsval[12];		/* See i2asc + 1 for null char. */
	unsigned char	asc_line_num[12];	/* To hold the ascii equivalent of the line_num. */
	unsigned char	*tmpnum, *end;
	mval		*spt;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (0 == p->e.count)
		return;
	count = (int)(TREF(mprof_ptr))->gvargs.count;
	spt = &(TREF(mprof_ptr))->subsc[count];
	/* Global name --> ^PREFIX(<OPTIONAL ARGUMENTS>, "rout-name", "label-name", "line-num", "forloop"). */
	spt->mvtype = MV_STR;
	spt->str.len = p->e.rout_name->len;
	spt->str.addr = (char *)pcalloc((unsigned int)spt->str.len);
	memcpy(spt->str.addr, p->e.rout_name->addr, spt->str.len);
	(TREF(mprof_ptr))->gvargs.args[count++] = spt++;
	spt->mvtype = MV_STR;
	if (0 != p->e.label_name->len)
	{
		spt->str.len = p->e.label_name->len;
		spt->str.addr = (char *)pcalloc((unsigned int)spt->str.len);
		memcpy(spt->str.addr, p->e.label_name->addr, spt->str.len);
	} else
	{	/* Place holder before first label. */
		spt->str.len = SIZEOF(MPROF_NULL_LABEL) - 1;
		spt->str.addr = (char *)pcalloc((unsigned int)spt->str.len);
		memcpy(spt->str.addr, MPROF_NULL_LABEL, spt->str.len);
	}
	(TREF(mprof_ptr))->gvargs.args[count++] = spt++;
	spt->mvtype = MV_STR;
	if (0 <= p->e.line_num)
	{
		tmpnum = i2asc(asc_line_num, (unsigned int) p->e.line_num);
		spt->str.len = INTCAST(tmpnum - asc_line_num);
		spt->str.addr = (char *)pcalloc((unsigned int)spt->str.len);
		memcpy(spt->str.addr, asc_line_num, spt->str.len);
		(TREF(mprof_ptr))->gvargs.args[count] = spt;
		count++;
		spt++;
	}
	/* For FOR loops. */
	if (is_for)
	{
		spt->mvtype = MV_STR;
		spt->str.len = SIZEOF(MPROF_FOR_LOOP) - 1;
		spt->str.addr = (char *)pcalloc(SIZEOF(MPROF_FOR_LOOP) - 1);
		memcpy(spt->str.addr, MPROF_FOR_LOOP, spt->str.len);
		(TREF(mprof_ptr))->gvargs.args[count++] = spt++;
		/* Write for level into the subscript as well. */
		spt->mvtype = MV_STR;
		tmpnum = i2asc(subsval, p->e.loop_level);
		spt->str.len = INTCAST(tmpnum - subsval);
		spt->str.addr = (char *)pcalloc((unsigned int)spt->str.len);
		memcpy(spt->str.addr, subsval, spt->str.len);
		(TREF(mprof_ptr))->gvargs.args[count++] = spt++;
	}
	(TREF(mprof_ptr))->gvargs.count = count;
	callg((INTPTR_T(*)(intszofptr_t count_arg, ...))op_gvname, (gparam_list *)&(TREF(mprof_ptr))->gvargs);
	(TREF(mprof_ptr))->gvargs.count = (TREF(mprof_ptr))->curr_num_subscripts;
	/* Data --> 'count:cpu-time in user mode:cpu-time in sys mode:cpu-time total'. */
	start_point = (INTPTR_T)&dataval[0];
	/* get count */
	tmpnum = (unsigned char *)&dataval[0];
	end = i2asc(tmpnum, p->e.count);
	tmpnum += ((end - tmpnum) > 0) ? (end - tmpnum) : (tmpnum - end);
	/* For non-FOR stuff get CPU time as well. */
	if (!is_for)
	{
		*tmpnum = ':';
		tmpnum++;
		end = i2ascl(tmpnum, p->e.usr_time);
		tmpnum += ((end - tmpnum) > 0) ? (end - tmpnum) : (tmpnum - end);
		*tmpnum = ':';
		tmpnum++;
		end = i2ascl(tmpnum, p->e.sys_time);
		tmpnum += ((end - tmpnum) > 0) ? (end - tmpnum) : (tmpnum - end);
		*tmpnum = ':';
		tmpnum++;
		end = i2ascl(tmpnum, p->e.sys_time + p->e.usr_time);
		tmpnum += ((end - tmpnum) > 0) ? (end - tmpnum) : (tmpnum - end);
		*tmpnum = ':';
		tmpnum++;
		end = i2ascl(tmpnum, p->e.elp_time);
		tmpnum += ((end - tmpnum) > 0) ? (end - tmpnum) : (tmpnum - end);
	}
	data.mvtype = MV_STR;
	data.str.len = (((INTPTR_T)tmpnum - start_point) > 0)
		? ((int)((INTPTR_T)tmpnum - start_point)) : ((int)(start_point - (INTPTR_T)tmpnum));
	data.str.addr = (char *)pcalloc((unsigned int)data.str.len);
	memcpy(data.str.addr, dataval, data.str.len);
	op_gvput(&data);
	return;
}

/* Save total CPU times for the current and all child processes. */
STATICFNDEF void insert_total_times(boolean_t for_process)
{
	int		count;
	INTPTR_T	start_point;
	mval		data;
	char		dataval[96];		/* Big enough for data value. */
	unsigned char	*tmpnum, *end;
	mval		*spt;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	count = (int)(TREF(mprof_ptr))->gvargs.count;
	spt = &(TREF(mprof_ptr))->subsc[count];
	spt->mvtype = MV_STR;
	if (for_process)
	{
		spt->str.len = SIZEOF(PROCESS_TIME) - 1;
		spt->str.addr = (char *)pcalloc(SIZEOF(PROCESS_TIME) - 1);
		memcpy(spt->str.addr, PROCESS_TIME, spt->str.len);
	} else
	{
		spt->str.len = SIZEOF(CHILDREN_TIME) - 1;
		spt->str.addr = (char *)pcalloc(SIZEOF(CHILDREN_TIME) - 1);
		memcpy(spt->str.addr, CHILDREN_TIME, spt->str.len);
	}
	(TREF(mprof_ptr))->gvargs.args[count++] = spt++;
	(TREF(mprof_ptr))->gvargs.count = count;
	callg((INTPTR_T(*)(intszofptr_t count_arg, ...))op_gvname, (gparam_list *)&(TREF(mprof_ptr))->gvargs);
	(TREF(mprof_ptr))->gvargs.count = (TREF(mprof_ptr))->curr_num_subscripts;
	start_point = (INTPTR_T)&dataval[0];
	tmpnum = (unsigned char *)&dataval[0];
	if (for_process)
		end = i2ascl(tmpnum, process_user);
	else
		end = i2ascl(tmpnum, child_user);
	tmpnum += ((end - tmpnum) > 0) ? (end - tmpnum) : (tmpnum - end);
	*tmpnum = ':';
	tmpnum++;
	if (for_process)
		end = i2ascl(tmpnum, process_system);
	else
		end = i2ascl(tmpnum, child_system);
	tmpnum += ((end - tmpnum) > 0) ? (end - tmpnum) : (tmpnum - end);
	*tmpnum = ':';
	tmpnum++;
	if (for_process)
		end = i2ascl(tmpnum, process_user + process_system);
	else
		end = i2ascl(tmpnum, child_user + child_system);
	tmpnum += ((end - tmpnum) > 0) ? (end - tmpnum) : (tmpnum - end);
	data.mvtype = MV_STR;
	data.str.len = (((INTPTR_T)tmpnum - start_point) > 0)
		? ((int)((INTPTR_T)tmpnum - start_point)) : ((int)(start_point - (INTPTR_T)tmpnum));
	data.str.addr = (char *)pcalloc((unsigned int)data.str.len);
	memcpy(data.str.addr, dataval, data.str.len);
	op_gvput(&data);
	return;
}

/* Fills the information about the current location in the code into the passed variable. */
STATICFNDEF void get_entryref_information(boolean_t line, trace_entry *tmp_trc_tbl_entry)
{
	boolean_t	line_reset;
	lab_tabent	*max_label, *label_table, *last_label;
	rhdtyp		*routine;
	stack_frame	*fp;
	int		status;
	unsigned char	*addr, *out_addr;
	int4		*line_table, *last_line, len, ct;
	int4		offset, in_addr_offset;
	unsigned long	user_time, system_time;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	line_reset = FALSE;
	for (fp = frame_pointer; fp; fp = fp->old_frame_pointer)
	{
		SKIP_BASE_FRAMES(fp);		/* Updates fp */
		assert(fp);
		if (ADDR_IN_CODE(fp->mpc, fp->rvector))
		{
			if (line_reset)
				addr = fp->mpc + 1;
			else
				addr = fp->mpc;
			break;
		} else
		{
			if (fp->type & SFT_ZTRAP || fp->type & SFT_DEV_ACT)
				line_reset = TRUE;
		}
	}
	if (NULL == fp)
	{
		TREF(prof_fp) = NULL;
		return;
	}
	routine = fp->rvector;
	routine = CURRENT_RHEAD_ADR(routine);
	label_table = LABTAB_ADR(routine);
	last_label = label_table + routine->labtab_len;
	max_label = label_table++;
	while (label_table < last_label)
	{
		if (addr > LABEL_ADDR(routine, label_table))
		{
			if (max_label->LABENT_LNR_OFFSET <= label_table->LABENT_LNR_OFFSET)
				max_label = label_table;
		}
		label_table++;
	}
	if (line)
	{
		tmp_trc_tbl_entry->rout_name = &routine->routine_name;
		tmp_trc_tbl_entry->label_name = &max_label->lab_name;
	}
	if (NULL == (TREF(prof_fp))->rout_name)
		(TREF(prof_fp))->rout_name = &routine->routine_name;
	if (NULL == (TREF(prof_fp))->label_name)
		(TREF(prof_fp))->label_name = &max_label->lab_name;
	if (!line)
		return;
	line_table = LABENT_LNR_ENTRY(routine, max_label);
	offset = 0;
	in_addr_offset = (int4)CODE_OFFSET(routine, addr);
	last_line = LNRTAB_ADR(routine);
	last_line += routine->lnrtab_len;
	for( ; ++line_table < last_line ; offset++)
	{
		if (in_addr_offset <= *line_table)
			break;
	}
	tmp_trc_tbl_entry->line_num = offset;
	return;
}

/* Parses the global variable name that the information will be dumped into, to make sure it is a valid gvn. */
STATICFNDEF void parse_gvn(mval *gvn)
{
	boolean_t 		dot_seen;
	mval			*spt;
	char			*c_top, *c_ref, ch;
	unsigned int		count = 0;
	char			*mpsp;		/* Pointer into mprof_mstr area. */
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	c_ref = gvn->str.addr;
	c_top = c_ref + gvn->str.len;
	if (!gvn->str.len || ('^' != *c_ref++))
		MPROF_RTS_ERROR((CSA_ARG(NULL) VARLSTCNT(4) ERR_NOTGBL, 2, gvn->str.len, gvn->str.addr));
	if (mprof_mstr.len < 4 * gvn->str.len)
	{	/* We are going to return an array of mvals pointing to global-name and subscript. We should
		 * never be needing more than 4 * gvn->str.len since the only expandable entity that can be
		 * passed is $j which uses up at least 3 characters (including '_' or ',') and expands to a
		 * maximum of 10 characters (see djbuff in getjobname.c).
		 */
		if (mprof_mstr.len)
		{
			assert(mprof_mstr.addr);
			free(mprof_mstr.addr);
		}
		mprof_mstr.len = 4 * gvn->str.len;
		mprof_mstr.addr = (char *)malloc(mprof_mstr.len);
	}
	mpsp = mprof_mstr.addr;
	/* Parse the global variable passed to insert the information. */
	spt = &(TREF(mprof_ptr))->subsc[0];
	spt->mvtype = MV_STR;
	spt->str.addr = mpsp;
	if (c_ref >= c_top)
		RTS_ERROR_VIEWNOTFOUND("Invalid global name");
	ch = *mpsp++ = *c_ref++;
	if (!ISALPHA_ASCII(ch) && ('%' != ch))
		RTS_ERROR_VIEWNOTFOUND("Invalid global name");
	for ( ; (c_ref < c_top) && ('(' != *c_ref); )
	{
		ch = *mpsp++ = *c_ref++;
		if (!ISALNUM_ASCII(ch))
			RTS_ERROR_VIEWNOTFOUND("Invalid global name");
	}
	spt->str.len = INTCAST(mpsp - spt->str.addr);
	(TREF(mprof_ptr))->gvargs.args[count++] = spt++;
	spt->str.addr = (char *)mpsp;
	/* Process subscripts, if any. */
	if (c_ref++ < c_top)
	{
		for ( ; c_ref < c_top; )
		{
			spt->mvtype = MV_STR;
			ch = *c_ref;
			if ('\"' == ch)
			{
				c_ref++;
				for ( ; ; )
				{
					if (c_ref == c_top)
						RTS_ERROR_VIEWNOTFOUND("Right parenthesis expected");
					if ('\"' == *c_ref)
					{
						if (++c_ref == c_top)
							RTS_ERROR_VIEWNOTFOUND("Right parenthesis expected");
						if (*c_ref != '\"')
							break;
					}
					*mpsp++ = *c_ref++;
				}
			} else if ('$' == ch)
			{
				if (++c_ref == c_top)
					RTS_ERROR_VIEWNOTFOUND("Intrinsic value is incomplete");
				if (*c_ref != 'J' && *c_ref != 'j')
					RTS_ERROR_VIEWNOTFOUND("Intrinsic value passed is not $j");
				c_ref++; 	/* Past 'J'. */
				if ((c_ref < c_top) && (ISALPHA_ASCII(*c_ref)))
				{
					ch = *c_ref;
					if (c_top >= c_ref + 2)
					{
						if (((('O' == ch) || ('o' == ch))
						    && (('B' == *(c_ref + 1)) || ('b' == *(c_ref + 1)))))
							c_ref += 2;
						else
							RTS_ERROR_VIEWNOTFOUND("Intrinsic value passed is not $j");
					} else
						RTS_ERROR_VIEWNOTFOUND("Intrinsic value is incomplete");
				}
				assert(10 > dollar_job.str.len);	/* To take care of 4 * gvn->str.len allocation above. */
				memcpy(mpsp, dollar_job.str.addr, dollar_job.str.len);
				mpsp += dollar_job.str.len;
			} else
			{
				dot_seen = FALSE;
				if (!ISDIGIT_ASCII(ch) && ('.' != ch) && ('-' != ch) && ('+' != ch))
					RTS_ERROR_VIEWNOTFOUND("Improperly formatted numeric subscript");
				if ('.' == ch)
					dot_seen = TRUE;
				*mpsp++ = *c_ref++;
				for ( ; ; )
				{
					if (c_ref == c_top)
						RTS_ERROR_VIEWNOTFOUND("Right parenthesis expected");
					if (!ISDIGIT_ASCII(*c_ref))
					{
						if ('.' != *c_ref)
							break;
						else if (!dot_seen)
							dot_seen = TRUE;
						else
							RTS_ERROR_VIEWNOTFOUND("Improperly formatted numeric subscript");
					}
					*mpsp++ = *c_ref++;
				}
			}
			if (c_ref == c_top)
				RTS_ERROR_VIEWNOTFOUND("Right parenthesis expected");
			if ('_' == *c_ref)
			{
				c_ref++;
				continue;
			}
			spt->str.len = INTCAST(mpsp - spt->str.addr);
			if (MAX_GVSUBSCRIPTS <= count)
				MPROF_RTS_ERROR((CSA_ARG(NULL) VARLSTCNT(1) ERR_MAXNRSUBSCRIPTS));
			(TREF(mprof_ptr))->gvargs.args[count++] = spt++;
			if (',' != *c_ref)
				break;
			spt->str.addr = mpsp;
			c_ref++;
		}
		assert(c_ref <= c_top);
		if (c_ref >= c_top)
			RTS_ERROR_VIEWNOTFOUND("Right parenthesis expected");
		if (')' != *c_ref)
			RTS_ERROR_VIEWNOTFOUND("Right parenthesis expected");
		if (++c_ref < c_top)
			RTS_ERROR_VIEWNOTFOUND("There are trailing characters after the global name");
	}
	assert((char *)mpsp <= mprof_mstr.addr + mprof_mstr.len);	/* Ensure we haven't overrun the malloced buffer. */
	(TREF(mprof_ptr))->gvargs.count = count;
	(TREF(mprof_ptr))->curr_num_subscripts = (int)(TREF(mprof_ptr))->gvargs.count;
	return;
}

void stack_leak_check(void)
{
	int	var_on_cstack;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (GTMTRIG_ONLY((0 < gtm_trigger_depth) ||) (0 < TREF(gtmci_nested_level)))
		return;
	if (NULL == var_on_cstack_ptr)
		var_on_cstack_ptr = &var_on_cstack;
	if ((&var_on_cstack != var_on_cstack_ptr)
#	     ifdef __i386	/* For 32-bit Linux allow a two pointer variation to accommodate ZHELP. */
	     && ((SIZEOF(var_on_cstack) * 2) < ABS(&var_on_cstack - var_on_cstack_ptr))
#	     endif
	     )
		assertpro(FALSE);
	return;
}
