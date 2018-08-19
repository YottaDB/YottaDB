/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
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

#include "gtm_string.h"
#include "gtm_stdlib.h"
#include "gtm_inet.h"	/* Required for gtmsource.h */
#include "gtm_stdio.h"
#include "gtm_fcntl.h"	/* Needed for AIX's silly open to open64 translations */
#include "gtm_signal.h"
#include <stdarg.h>

#include "ast.h"
#include "gdsroot.h"
#include "gdsbt.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsblk.h"
#include "gdsfhead.h"
#include "gdscc.h"
#include "gdskill.h"
#include "gt_timer.h"
#include "gtmio.h"
#include "filestruct.h"
#include "gtmdbglvl.h"
#include "error.h"
#include "hashtab_mname.h"
#include "io.h"
#include "io_params.h"
#include "jnl.h"
#include "lv_val.h"
#include <rtnhdr.h>
#include "mv_stent.h"
#include "outofband.h"
#include "stack_frame.h"
#include "stringpool.h"
#include "hashtab_int4.h"	/* needed for tp.h */
#include "buddy_list.h"		/* needed for tp.h */
#include "tp.h"
#include "tp_frame.h"
#include "xfer_enum.h"
#include "mlkdef.h"
#include "repl_msg.h"
#include "gtmsource.h"
#include "zwrite.h"
#include "cache.h"
#include "cache_cleanup.h"
#include "objlabel.h"
#include "op.h"
#include "dpgbldir.h"
#include "preemptive_db_clnup.h"
#include "compiler.h"		/* needed for MAX_SRCLINE */
#include "show_source_line.h"
#include "trans_code_cleanup.h"
#include "dm_setup.h"
#include "util.h"
#include "tp_restart.h"
#include "dollar_zlevel.h"
#include "error_trap.h"
#include "golevel.h"
#include "send_msg.h"
#include "jobinterrupt_process_cleanup.h"
#include "fix_xfer_entry.h"
#include "change_reg.h"
#include "tp_change_reg.h"
#include "alias.h"
#include "create_fatal_error_zshow_dmp.h"
#include "have_crit.h"
#include "invocation_mode.h"
#include "iormdef.h"
#include "ftok_sems.h"
#include "gtm_putmsg_list.h"
#ifdef GTM_TRIGGER
# include "gv_trigger.h"
# include "gtm_trigger.h"
#endif
#include "libyottadb.h"
#include "setup_error.h"

GBLREF	boolean_t		created_core, dont_want_core, in_gvcst_incr, run_time;
GBLREF	boolean_t		ztrap_explicit_null;		/* whether $ZTRAP was explicitly set to NULL in this frame */
GBLREF	dollar_ecode_type	dollar_ecode;			/* structure containing $ECODE related information */
GBLREF	dollar_stack_type	dollar_stack;
GBLREF	gd_region		*gv_cur_region;
GBLREF	gv_key			*gv_currkey;
GBLREF	gv_namehead		*gv_target;
GBLREF	inctn_opcode_t		inctn_opcode;
GBLREF	int			mumps_status, pool_init;
GBLREF	int4			exi_condition;
GBLREF	io_desc			*active_device, *gtm_err_dev;
GBLREF	io_pair			io_std_device, io_curr_device;
GBLREF	jnlpool_addrs_ptr_t	jnlpool;
GBLREF	jnlpool_addrs_ptr_t	jnlpool_head;
GBLREF	mstr			*err_act;
GBLREF	mval			*alias_retarg, dollar_zstatus, dollar_zerror;
GBLREF	mv_stent		*mv_chain;
GBLREF	sgm_info		*first_sgm_info;
GBLREF	sgmnt_addrs		*cs_addrs;
GBLREF	sgmnt_data_ptr_t	cs_data;
GBLREF	spdesc			indr_stringpool, rts_stringpool, stringpool;
GBLREF	stack_frame		*frame_pointer, *zyerr_frame, *error_frame;
GBLREF	tp_frame		*tp_pointer;
GBLREF	tp_region		*tp_reg_list;		/* Chained list of regions in this transaction not cleared on tp_restart */
GBLREF	uint4			ydbDebugLevel;		/* Debug level */
GBLREF	uint4			process_id;
GBLREF	unsigned char		*msp, *restart_ctxt, *restart_pc, *stacktop, *stackwarn, *tp_sp, *tpstacktop, *tpstackwarn;
GBLREF	unsigned short		proc_act_type;
GBLREF	volatile bool		neterr_pending, std_dev_outbnd;
GBLREF	volatile boolean_t	dollar_zininterrupt;
GBLREF	volatile int4		gtmMallocDepth, outofband;
GBLREF	xfer_entry_t		xfer_table[];
#ifdef DEBUG
GBLREF	boolean_t		donot_INVOKE_MUMTSTART;
#endif
#ifdef GTM_TRIGGER
GBLREF	int			tprestart_state;	/* When triggers restart, multiple states possible - see tp_restart.h */
GBLREF	int4			gtm_trigger_depth;
#endif
GBLREF	jnl_gbls_t		jgbl;

error_def(ERR_ASSERT);
error_def(ERR_CTRAP);
error_def(ERR_CTRLC);
error_def(ERR_CTRLY);
error_def(ERR_GTMASSERT);
error_def(ERR_GTMASSERT2);
error_def(ERR_GTMCHECK);
error_def(ERR_GTMERREXIT);
error_def(ERR_JOBINTRRETHROW);
error_def(ERR_JOBINTRRQST);
error_def(ERR_LABELMISSING);
error_def(ERR_MEMORY);
error_def(ERR_NOEXCNOZTRAP);
error_def(ERR_NOTPRINCIO);
error_def(ERR_OUTOFSPACE);
error_def(ERR_REPEATERROR);
error_def(ERR_RESTART);
error_def(ERR_RTSLOC);
error_def(ERR_SRCLOCUNKNOWN);
error_def(ERR_STACKCRIT);
error_def(ERR_STACKOFLOW);
error_def(ERR_TLVLZERO);
error_def(ERR_TPRETRY);
error_def(ERR_TPRESTNESTERR);
error_def(ERR_TPSTACKCRIT);
error_def(ERR_TPSTACKOFLOW);
error_def(ERR_TPTIMEOUT);
error_def(ERR_UNSOLCNTERR);

boolean_t clean_mum_tstart(void);

/* When we restart generated code after handling an error, verify that we are not in the frame or one created on its
 * behalf that invoked a trigger or spanning node and caused a dynamic TSTART to be done on its behalf. This can happen
 * for example if a trigger is invoked for the first time but gets a compilation or link failure error or if a spanning
 * node fetch or update drives an error. In the trigger case, the relevant error is thrown from gtm_trigger() while no
 * trigger based error handling is in effect so no rollback of the dynamic frame occurs which results in unhandled TPQUIT
 * errors, perhaps interminably. In both the trigger and spanning-node cases, the MUM_TSTART we are about to execute unrolls
 * the C stack preventing any return to the C frame that did the implicit tstart and prevents it from being committed so
 * it must be rolled back.
 * Note: But no trollback should occur in case of a simpleAPI started TP (which is also an implicit TP). It is "ydb_tp_s"
 * that will do the trollback in that case. Hence the "!tp_pointer->ydb_tp_s_tstart" check below.
 */
#define MUM_TSTART_FRAME_CHECK												\
{															\
	if ((0 == gtm_trigger_depth) && tp_pointer && tp_pointer->implicit_tstart && !tp_pointer->ydb_tp_s_tstart)	\
	{														\
		DEBUG_ONLY(donot_INVOKE_MUMTSTART = FALSE);								\
		OP_TROLLBACK(-1);	/* Unroll implicit TP frame */							\
	}														\
}

/* Since a call to SET_ZSTATUS may cause the stringpool expansion, record whether we are using an indirection pool or not
 * in a local variable rather than recompare the stringpool bases.
 */
#define SAVE_ZSTATUS_INTO_RTS_STRINGPOOL(VAR, ARG)							\
{													\
	boolean_t using_indrpool;									\
													\
	using_indrpool = (stringpool.base != rts_stringpool.base);					\
	if (using_indrpool)										\
	{												\
		indr_stringpool = stringpool;   /* update indr_stringpool */				\
		stringpool = rts_stringpool;    /* change for set_zstatus */				\
	}												\
	VAR = SET_ZSTATUS(ARG);										\
	if (using_indrpool)										\
	{												\
		rts_stringpool = stringpool;    /* update rts_stringpool */				\
		stringpool = indr_stringpool;   /* change back */					\
	}												\
}

/* We ignore errors in the $ZYERROR routine. When an error occurs, we unwind all stack frames upto and including
 * zyerr_frame. MUM_TSTART then transfers control to the $ZTRAP frame.
 */
boolean_t clean_mum_tstart(void)
{
	stack_frame	*save_zyerr_frame, *fp, *fpprev;
	boolean_t	save_check_flag;

	if (NULL != zyerr_frame)
	{
		while ((NULL != frame_pointer) && (NULL != zyerr_frame))
		{
			GOFRAMES(1, TRUE, FALSE);
		}
		assert(NULL != frame_pointer);
		proc_act_type = 0;
		if (indr_stringpool.base == stringpool.base)
		{ /* switch to run time stringpool */
			indr_stringpool = stringpool;
			stringpool = rts_stringpool;
		}
		return TRUE;
	}
	return (NULL != err_act);
}

CONDITION_HANDLER(mdb_condition_handler)
{
	unsigned char		*cp, *context;
	boolean_t		dm_action;	/* did the error occur on a action from direct mode */
	boolean_t		trans_action;	/* did the error occur during "transcendental" code */
	boolean_t		prev_frame_is_indce;	/* used to compute "dm_action" */
	char			src_line[MAX_ENTRYREF_LEN];
	mstr			src_line_d;
	io_desc			*err_dev;
	tp_region		*tr;
	gd_region		*reg_top, *reg_save, *reg_local;
	gd_addr			*addr_ptr;
	jnlpool_addrs_ptr_t	local_jnlpool;
	sgmnt_addrs		*csa;
	stack_frame		*fp;
	boolean_t		error_in_zyerror;
	boolean_t		repeat_error, etrap_handling, reset_mpc;
	int			level, rc;
	boolean_t		reserve_sock_dev = FALSE;
	unix_db_info		*udi;
	stack_frame		*lcl_error_frame;
	mv_stent		*mvst;

	START_CH(FALSE);
	DBGEHND((stderr, "mdb_condition_handler: Entered with SIGNAL=%d frame_pointer=0x"lvaddr"\n", SIGNAL, frame_pointer));
	if (NULL != gtm_err_dev)
	{
		/* It is possible that we entered here from a bad compile of the OPEN exception handler
		 * for a device.  If gtm_err_dev is still set from the previous mdb_condition_handler
		 * invocation that drove the error handler that occurred during the OPEN command, then its
		 * structures should be released now.
		 */
		if (gtmsocket == gtm_err_dev->type)
			iosocket_destroy(gtm_err_dev);
		else
			remove_rms(gtm_err_dev);
		gtm_err_dev = NULL;
	}
	if (repeat_error = (ERR_REPEATERROR == SIGNAL)) /* assignment and comparison */
		SIGNAL = dollar_ecode.error_last_ecode;
	DEBUG_ONLY(TREF(LengthReentCnt) = TREF(ZLengthReentCnt) = 0);
	preemptive_db_clnup(SEVERITY);
	assert(NULL == alias_retarg);
	if (NULL != alias_retarg)
		CLEAR_ALIAS_RETARG;
	assert(NULL == TREF(gtmci_retval));
	TREF(gtmci_retval) = NULL;		/* Clear any dangling set */
	if ((int)ERR_UNSOLCNTERR == SIGNAL)
	{
		/* This is here for linking purposes.  We want to delay the receipt of
		 * network errors in gtm until we are ready to deal with them.  Hence
		 * the transfer table hijinx.  To avoid doing this in the gvcmz routine,
		 * we signal the error and do it here
		 */
		neterr_pending = TRUE;
                FIX_XFER_ENTRY(xf_linefetch, op_fetchintrrpt);
                FIX_XFER_ENTRY(xf_linestart, op_startintrrpt);
                FIX_XFER_ENTRY(xf_forchk1, op_startintrrpt);
                FIX_XFER_ENTRY(xf_forloop, op_forintrrpt);
		CONTINUE;
	}
	MDB_START;
	assert(FALSE == in_gvcst_incr);	/* currently there is no known case where this can be TRUE at this point */
	in_gvcst_incr = FALSE;	/* reset this just in case gvcst_incr/gvcst_put failed to do a good job of resetting */
	inctn_opcode = inctn_invalid_op;
	/* Ideally merge should have a condition handler to reset followings, but generated code can call other routines
	 * during MERGE command (MERGE command invokes multiple op-codes depending on source vs target). So it is not
	 * easy to establish a condition handler there. Easy solution is following one line code.
	 */
	NULLIFY_MERGE_ZWRITE_CONTEXT;
	if ((int)ERR_TPRETRY == SIGNAL)
	{
		lcl_error_frame = error_frame;
		if ((NULL == lcl_error_frame) && dollar_zininterrupt)
		{	/* We are in a $zininterrupt handler AND have a restart request. See if we were in error processing
			 * when we started the interrupt. We can locate this in the zinterrupt mv_stent. Note this loop
			 * does not process the last mv_stent on the stack but since that is the MVST_STORIG entry, this is ok
			 * as we expect to find a ZINTRupt entry long before that.
			 */
			for (mvst = mv_chain; 0 != mvst->mv_st_next; mvst = (mv_stent *)(mvst->mv_st_next + (char *)mvst))
			{
				if (MVST_ZINTR == mvst->mv_st_type)
					break;
			}
			assertpro(MVST_ZINTR == mvst->mv_st_type); /* No zinterrupt block, big problemo */
			lcl_error_frame = mvst->mv_st_cont.mvs_zintr.error_frame_save;
		}
		if (NULL == lcl_error_frame)
		{
#			ifdef GTM_TRIGGER
			/* Assert that we never end up invoking the MUM_TSTART macro handler in case of an implicit tstart restart.
			 * See GBLDEF of skip_INVOKE_RESTART and donot_INVOKE_MUMTSTART in gbldefs.c for more information.
			 * Note that it is possible for this macro to be invoked from generated code in a trigger frame (in which
			 * case gtm_trigger/tp_restart ensure control passed to mdb_condition_handler only until the outermost
			 * implicit tstart in which case they return). Assert accordingly.
			 */
			assert(!donot_INVOKE_MUMTSTART || gtm_trigger_depth);
			TRIGGER_BASE_FRAME_UNWIND_IF_NOMANSLAND;
#			endif
			rc = tp_restart(1, TP_RESTART_HANDLES_ERRORS);
			DBGEHND((stderr, "mdb_condition_handler: tp_restart returned with rc=%d. state=%d, and SIGNAL=%d\n",
				 rc, GTMTRIG_ONLY(tprestart_state) NON_GTMTRIG_ONLY(0), error_condition));
#			ifdef GTM_TRIGGER
			if (0 != rc)
			{	/* The only time "tp_restart" will return non-zero is if the error needs to be
				 * rethrown. To accomplish that, we will unwind this handler which will return to
				 * the inner most initiating dm_start() with the return code set to whatever mumps_status
				 * is set to.
				 */
				assert(TPRESTART_STATE_NORMAL != tprestart_state);
				assert(rc == SIGNAL);
				assertpro((SFT_TRIGR & frame_pointer->type) && (0 < gtm_trigger_depth));
				mumps_status = rc;
				DBGEHND((stderr, "mdb_condition_handler: Unwind-return to caller (gtm_trigger)\n"));
				/* It is possible that dollar_tlevel at the time of the ESTABLISH of mdb_condition_handler
				 * was higher than the current dollar_tlevel. This is because tp_restart done above could
				 * have decreased dollar_tlevel. Even though dollar_tlevel before the UNWIND done
				 * below is not the same as that at ESTABLISH_RET time, the flow of control bubbles back
				 * correctly to the op_tstart at $tlevel=1 and resumes execution. So treat this as an exception
				 * and adjust active_ch->dollar_tlevel so it is in sync with the current dollar_tlevel. This
				 * prevents an assert failure in UNWIND. START_CH would have done a active_ch-- So we need a
				 * active_ch[1] to get at the desired active_ch. See similar code in tp_restart.c.
				 */
				assert(active_ch[1].dollar_tlevel >= dollar_tlevel);
				DEBUG_ONLY(active_ch[1].dollar_tlevel = dollar_tlevel);
				UNWIND(NULL, NULL);
			}
			/* "tp_restart" has succeeded so we have unwound back to the return point but check if the
			 * transaction was initiated by an implicit trigger TSTART. This can occur if an error was
			 * encountered in a trigger before the trigger base-frame was setup. It can occur at any trigger
			 * level if a triggered update is preceeded by a TROLLBACK.
			 */
			if (!(SFT_TRIGR & frame_pointer->type) && tp_pointer && tp_pointer->implicit_tstart)
			{
				if (tp_pointer->ydb_tp_s_tstart)
				{	/* This is a TP transaction started by a "ydb_tp_s" call. Since "mdb_condition_handler"
					 * is handling the TPRESTART error, it is a call-in. So set return code to ERR_TPRETRY
					 * that way the "ydb_ci" call can return this to the caller and that can take
					 * appropriate action. Note that we should not use YDB_TP_RESTART here as that
					 * is a negative value returned only by the simpleAPI (ydb_*_s*() functions) and
					 * not "ydb_ci".
					 */
					rc = ERR_TPRETRY;
				}
				mumps_status = rc;
				DBGEHND((stderr, "mdb_condition_handler: Returning to implicit TSTART originator\n"));
				/* Do dbg-only dollar_tlevel adjustment just like done in previous UNWIND invocation */
				assert(active_ch[1].dollar_tlevel >= dollar_tlevel);
				DEBUG_ONLY(active_ch[1].dollar_tlevel = dollar_tlevel);
				UNWIND(NULL, NULL);
			}
			assert(!donot_INVOKE_MUMTSTART);
#			endif
			if (ERR_TPRETRY == SIGNAL)		/* (signal value undisturbed) */
			{
				/* Set mumps program counter back tstart level 1 */
				MUM_TSTART;
			}
		} else
		{
			if (0 == dollar_tlevel)
				SIGNAL = ERR_TLVLZERO;		/* TPRESTART specified but not in TP */
			else
				SIGNAL = ERR_TPRESTNESTERR;	/* Only if actually in TP */
			/* TPRETRY encountered or requested during error handling - treat as nested error to prevent issues
			 * with errors being rethrown during a TP restart. Change the error from TPRETRY to either TLVLZERO or
			 * TPRESTNESTERR as appropriate so we don't give internal use only error name to user and let error
			 * continue through regular processing (both treated as nested error since error_frame non-NULL).
			 */
			setup_error(gv_target ? gv_target->gd_csa : NULL, VARLSTCNT(1) SIGNAL);
		}
	}
	/* Ensure gv_target and cs_addrs are in sync. If not, make them so. */
	if (NULL != gv_target)
	{
		csa = gv_target->gd_csa;
		if (NULL != csa)
		{
			if (csa != cs_addrs)
			{
				assert(0 < csa->regcnt);
				/* If csa->regcnt is > 1, it is possible that csa->region is different from the actual gv_cur_region
				 * (before we encountered the runtime error). This is a case of two regions mapping to the same csa.
				 * The only issue with this is that some user-level error messages that have the region name (as
				 * opposed to the database file name) could print incorrect values. But other than that there should
				 * be no issues since finally the csa (corresponding to the physical database file) is what matters
				 * and that is the same for both the regions. Given that the region mismatch potential exists only
				 * until the next global reference which is different from $REFERENCE, we consider this acceptable.
				 */
				gv_cur_region = csa->region;
				assert(gv_cur_region->open);
				assert(IS_REG_BG_OR_MM(gv_cur_region));
				/* The above assert is needed to ensure that change_reg/tp_change_reg (invoked below)
				 * will set cs_addrs, cs_data etc. to non-zero values.
				 */
				if (NULL != first_sgm_info)
					change_reg(); /* updates "cs_addrs", "cs_data", "sgm_info_ptr" and maybe "first_sgm_info" */
				else
				{	/* We are either inside a non-TP transaction or a TP transaction that has done NO database
					 * references. In either case, we do NOT want to setting sgm_info_ptr or first_sgm_info.
					 * Hence use tp_change_reg instead of change_reg below.
					 */
					tp_change_reg(); /* updates "cs_addrs", "cs_data" */
				}
				assert(cs_addrs == csa);
				assert(cs_data == csa->hdr);
				assert(NULL != cs_data);
			}
			/* Fix gv_currkey to null-str in case gv_target points to dir_tree (possible in case of name-level-$order).
			 * Do same in case gv_target points to cs_addrs->hasht_tree so we dont take the fast path in op_gvname
			 * when gv_target is clearly not GVT of a user-visible global.
			 */
			if ((gv_target == csa->dir_tree) GTMTRIG_ONLY(|| (gv_target == csa->hasht_tree)))
			{
				gv_currkey->end = 0;
				gv_currkey->base[0] = 0;
			}
		}
	}
	DBG_CHECK_GVTARGET_GVCURRKEY_IN_SYNC(CHECK_CSA_TRUE);
	if (DUMPABLE)
	{	/* Certain conditions we don't want to attempt to create the M-level ZSHOW dump.
		 * 1) If gtmMallocDepth > 0 indicating memory manager was active and could be reentered.
		 * 2) If we have a SIGBUS or SIGSEGV (could be likely to occur again
		 *    in the local variable code which would cause immediate shutdown with
		 *    no cleanup).
		 * Note that we will bypass check 2 if GDL_ZSHOWDumpOnSignal debug flag is on
		 */
		SET_PROCESS_EXITING_TRUE;	/* So zshow doesn't push stuff on stack to "protect" it when
						 * we potentially already have a stack overflow */
		CANCEL_TIMERS;			/* No unsafe interruptions now that we are dying */
		if (!repeat_error && (0 == gtmMallocDepth))
		{
			src_line_d.addr = src_line;	/* Setup entry point buffer for set_zstatus() */
			src_line_d.len = 0;
			SET_ZSTATUS(NULL);
		}
		/* Create the ZSHOW dump file if it can be created */
		create_fatal_error_zshow_dmp(SIGNAL);

		/* If we are about to core/exit on a stack over flow, only do the core part if a debug
		 * flag requests this behaviour. Otherwise, supress the core and just exit.
		 * 2008-01-29 (se): Added fatal MEMORY error so we no longer generate a core for it by
		 * default unless the DumpOnStackOFlow flag is turned on. Since this flag is not a user-exposed
		 * interface, I'm avoiding renaming it for now.
		 *
		 * Finally note that in UNIX, ch_cond_core (called by DRIVECH macro which invoked this condition
		 * handler has likely already created the core and set the created_core flag which will prevent
		 * this process from creating another core for the same SIGNAL. We leave this code in here in
		 * case methods exist in the future for this module to be driven without invoking cond_core_ch
		 * first.
		 */
		if (!(GDL_DumpOnStackOFlow & ydbDebugLevel) &&
		    ((int)ERR_STACKOFLOW == SIGNAL || (int)ERR_STACKOFLOW == arg
		     || (int)ERR_MEMORY == SIGNAL || (int)ERR_MEMORY == arg))
		{
			MUMPS_EXIT;	/* Do a clean exit rather than messy core exit */
		}
		TERMINATE;
	}
#	ifdef GTM_TRIGGER
	assertpro(TPRESTART_STATE_NORMAL == tprestart_state);	/* Can't leave half-restarted transaction around - out of design */
#	endif
	if (TREF(xecute_literal_parse))
	{	/* This is not expected but protect against it */
		assert(!TREF(xecute_literal_parse));
		run_time = TREF(xecute_literal_parse) = FALSE;
	}
	UNDO_ACTIVE_LV(actlv_mdb_condition_handler);
	/*
	 * If error is at least severity "WARNING", do some cleanups. Note: crit is no longer unconditionally
	 * released here. It is now released if NOT in TP (any retry) or if in TP but NOT in the final retry.
	 * But we still cleanup the replication instance file lock and the replication crit lock. We do this
	 * because when starting the final retry GTM only holds the grab_crit locks but not the grab_lock or
	 * ftok_sem_lock locks. The grab_lock is done only at commit time (at which point there cannot be any
	 * programmatic runtime errors possible). Errors inside jnlpool_init (which cause the ftok-sem-lock
	 * to be held) can never be programmatically caused.
	 *
	 * On the other hand, while running in the final retry holding crit, a program can cause an arbitrary
	 * error (e.g. 1/0 error or similar). Hence the distinction. The reasoning is that any of these could
	 * potentially take an arbitrary amount of time and we don't want to be holding critical locks while
	 * doing these. But one can argue that the error trap is nothing but an extension of the transaction
	 * and that if GT.M is fine executing arbitrary application M code in the final retry holding crit,
	 * it should be fine doing the same for the error trap M code as well. So the entire point of not
	 * releasing crit is to avoid indefinite TP retries due to runtime errors. But this means the error
	 * trap should be well coded (i.e. not have any long running commands etc.) to avoid crit hangs
	 * and/or deadlocks.
	 */
	if ((SUCCESS != SEVERITY) && (INFO != SEVERITY))
	{
		ENABLE_AST;
		if ((0 == dollar_tlevel) || (CDB_STAGNATE > t_tries))
		{	/* Only release crit if we are NOT in TP *or* if we are in TP, we aren't in final retry */
			for (addr_ptr = get_next_gdr(NULL); addr_ptr; addr_ptr = get_next_gdr(addr_ptr))
			{
				for (reg_local = addr_ptr->regions, reg_top = reg_local + addr_ptr->n_regions;
				     reg_local < reg_top; reg_local++)
				{
					if (reg_local->open && !reg_local->was_open)
					{
						csa = (sgmnt_addrs *)&FILE_INFO(reg_local)->s_addrs;
						if (csa && csa->now_crit)
						{
							assert(!csa->hold_onto_crit UNIX_ONLY(|| jgbl.onlnrlbk));
							if (csa->hold_onto_crit)
								csa->hold_onto_crit = FALSE; /* Fix it in pro */
							rel_crit(reg_local);
						}
					}
				}
			}
		}
		/* Release FTOK lock on the replication instance file if holding it (possible if error in jnlpool_init) */
		for (local_jnlpool = jnlpool_head; local_jnlpool; local_jnlpool = local_jnlpool->next)
		{
			if ((NULL != local_jnlpool->jnlpool_dummy_reg) && local_jnlpool->jnlpool_dummy_reg->open)
			{
				udi = FILE_INFO(local_jnlpool->jnlpool_dummy_reg);
				assert(NULL != udi);
				if (NULL != udi)
				{
					if (udi->grabbed_ftok_sem)
						ftok_sem_release(local_jnlpool->jnlpool_dummy_reg, FALSE, FALSE);
					assert(!udi->grabbed_ftok_sem);
				}
			}
		}
		/* Release crit lock on journal pool if holding it */
		for (local_jnlpool = jnlpool_head; local_jnlpool; local_jnlpool = local_jnlpool->next)
			if (local_jnlpool->pool_init)
			{
				csa = (sgmnt_addrs *)&FILE_INFO(local_jnlpool->jnlpool_dummy_reg)->s_addrs;
				if (csa && csa->now_crit)
					rel_lock(local_jnlpool->jnlpool_dummy_reg);
			}
		TREF(in_op_fnnext) = FALSE;			/* in case we were in $NEXT */
		/* Global values that may need cleanups */
		if (INTRPT_OK_TO_INTERRUPT != intrpt_ok_state)
			ENABLE_INTERRUPTS(intrpt_ok_state, INTRPT_OK_TO_INTERRUPT);	/* If interrupts were deferred,
											 * re-enable them now */
	}
#	ifdef GTM_TRIGGER
	/* At this point, we are past the point where the frame pointer is allowed to be resting on a trigger frame
	 * (this is possible in a TPRETRY situation where gtm_trigger must return to gtm_trigger() signaling a
	 * restart is necessary). If we are on a trigger base frame, unwind it so the error is recognized in
	 * the invoker's frame.
	 */
	if (SFT_TRIGR & frame_pointer->type)
	{
		/* Better be an error in here info or success messages want to continue, not be unwound but
		 * we cannot go past this point in a trigger frame or the frame_pointer back reference below
		 * will fail.
		 */
		assert((SUCCESS != SEVERITY) && (INFO != SEVERITY));
		/* These outofband conditions depend on saving the current stack frame info in restart_pc which
		 * is of course no longer valid once the frame is unrolled so they must be avoided. At the time
		 * of this writing, there are no conditions that these should validly be called in this
		 * situation so this check is more for the future.
		 */
		assert(((int)ERR_CTRLY != SIGNAL) && ((int)ERR_CTRLC != SIGNAL) && ((int)ERR_CTRAP != SIGNAL)
		       && ((int)ERR_JOBINTRRQST != SIGNAL) && ((int)ERR_JOBINTRRETHROW != SIGNAL));
		gtm_trigger_fini(TRUE, FALSE);
		DBGEHND((stderr, "mdb_condition_handler: Current trigger frame unwound so error is thrown"
			 " on trigger invoker's frame instead.\n"));
	}
#	endif
 	err_dev = active_device;
	active_device = (io_desc *)NULL;
	/* Determine if the error occurred on a action from direct mode.
	 * Go back until we find a counted frame. If we see a SFF_INDCE frame and a SFT_DM frame
	 * before then, we are in direct mode. Else we are not in direct mode.
	 * Note that it is also possible for a compilation error before the SFF_INDCE frame is created in op_commarg.
	 * That is also a direct mode error so take that into account separately (TREF(compile_time) usage below).
	 */
	dm_action = FALSE;
	prev_frame_is_indce = TREF(compile_time);
	for (fp = frame_pointer; ; fp = fp->old_frame_pointer)
	{
		assert(!(fp->type & SFT_COUNT) || !(fp->type & SFT_DM));
		if (fp->type & SFT_COUNT)
			break;
		if (fp->type & SFT_DM)
		{
			if (prev_frame_is_indce)
				dm_action = TRUE;
			break;
		}
		prev_frame_is_indce = (fp->flags & SFF_INDCE);
		assert(NULL != fp->old_frame_pointer);
	}
	/* The errors are said to be transcendental when they occur during compilation/execution
	 * of the error trap ({z,e}trap, device exception) or $zinterrupt. The errors in other
	 * indirect code frames (zbreak, zstep, xecute etc.) aren't defined to be trancendental
	 * and will be treated  as if they occured in a regular code frame.
	 */
	trans_action = proc_act_type || (frame_pointer->type & SFT_ZTRAP) || (frame_pointer->type & SFT_DEV_ACT);
	src_line_d.addr = src_line;
	src_line_d.len = 0;
	flush_pio();
	if ((int)ERR_CTRLY == SIGNAL)
	{
		outofband_clear();
		/* Verify not indirect or that context is unchanged before reset context */
		assert(NULL != restart_pc);
		assert((!(SFF_INDCE & frame_pointer->flags)) || (restart_ctxt == frame_pointer->ctxt));
		DBGEHND((stderr, "mdb_condition_handler(1): Resetting frame 0x"lvaddr" mpc/context with restart_pc/ctxt "
			 "0x"lvaddr"/0x"lvaddr" - frame has type 0x%04lx\n", frame_pointer, restart_pc, restart_ctxt,
			 frame_pointer->type));
		frame_pointer->mpc = restart_pc;
		frame_pointer->ctxt = restart_ctxt;
		MUM_TSTART;
	} else  if ((int)ERR_CTRLC == SIGNAL)
	{
		outofband_clear();
		if (!trans_action && !dm_action)
		{	/* Verify not indirect or that context is unchanged before reset context */
			assert(NULL != restart_pc);
			assert((!(SFF_INDCE & frame_pointer->flags)) || (restart_ctxt == frame_pointer->ctxt));
			DBGEHND((stderr, "mdb_condition_handler(2): Resetting frame 0x"lvaddr" mpc/context with restart_pc/ctxt "
				 "0x"lvaddr"/0x"lvaddr" - frame has type 0x%04lx\n", frame_pointer, restart_pc, restart_ctxt,
				 frame_pointer->type));
			frame_pointer->mpc = restart_pc;
			frame_pointer->ctxt = restart_ctxt;
			frame_pointer->flags &= SSF_NORET_VIA_MUMTSTART_OFF;	/* Frame enterable now with mpc reset */
			GTMTRIG_ONLY(
				DBGTRIGR((stderr, "mdb_condition_handler: disabling SSF_NORET_VIA_MUMTSTART_OFF (1) in frame "
					  "0x"lvaddr"\n", frame_pointer)));
			if (!(frame_pointer->type & SFT_DM))
				dm_setup();
		} else  if (frame_pointer->type & SFT_DM)
		{
			frame_pointer->ctxt = GTM_CONTEXT(call_dm);
			frame_pointer->mpc = CODE_ADDRESS(call_dm);
			frame_pointer->flags &= SSF_NORET_VIA_MUMTSTART_OFF;	/* Frame enterable now with mpc reset */
			GTMTRIG_ONLY(
				DBGTRIGR((stderr, "mdb_condition_handler: disabling SSF_NORET_VIA_MUMTSTART_OFF (1) in frame "
					  "0x"lvaddr"\n", frame_pointer)));
		} else
		{
			/* Do cleanup on indirect frames prior to reset */
			IF_INDR_FRAME_CLEANUP_CACHE_ENTRY_AND_UNMARK(frame_pointer);
			frame_pointer->ctxt = GTM_CONTEXT(pseudo_ret);
			frame_pointer->mpc = CODE_ADDRESS(pseudo_ret);
			frame_pointer->flags &= SSF_NORET_VIA_MUMTSTART_OFF;	/* Frame enterable now with mpc reset */
			GTMTRIG_ONLY(
				DBGTRIGR((stderr, "mdb_condition_handler: disabling SSF_NORET_VIA_MUMTSTART_OFF (1) in frame "
					  "0x"lvaddr"\n", frame_pointer)));
		}
		PRN_ERROR;
		if (io_curr_device.out != io_std_device.out)
			dec_err(VARLSTCNT(4) ERR_NOTPRINCIO, 2, io_curr_device.out->trans_name->len,
				io_curr_device.out->trans_name->dollar_io);
		MUM_TSTART;
	} else if ((int)ERR_CTRAP == SIGNAL)
	{
		if (!repeat_error)
			/* This has already been done if we are re-throwing the error */
			outofband_clear();
		if (!trans_action && !dm_action && !(frame_pointer->type & SFT_DM))
		{
			if (!repeat_error)
			{
				SAVE_ZSTATUS_INTO_RTS_STRINGPOOL(dollar_ecode.error_last_b_line, NULL);
			}
			assert(NULL != dollar_ecode.error_last_b_line);
			/* Only (re)set restart_pc if we are in the original frame. This is needed to restart execution at the
			 * beginning of the line but only happens when $ZTRAP is in effect. If $ETRAP is in control, control
			 * naturally returns to the caller unless the $ECODE value is not set. In that case, the CTRAP error
			 * will be rethrown one level down as $ETRAP normally does. Note in case of a rethrow, we avoid resetting
			 * mpc/ctxt as those values are only appropriate for the level in which they were saved.
			 */
			if (!repeat_error && ((0 != (TREF(dollar_ztrap)).str.len) || ztrap_explicit_null))
			{	/* Verify not indirect or that context is unchanged before reset context */
				assert(NULL != restart_pc);
				assert((!(SFF_INDCE & frame_pointer->flags)) || (restart_ctxt == frame_pointer->ctxt));
				DBGEHND((stderr, "mdb_condition_handler(3): Resetting frame 0x"lvaddr" mpc/context with restart_pc/"
					 "ctxt 0x"lvaddr"/0x"lvaddr" - frame has type 0x%04lx\n", frame_pointer, restart_pc,
					 restart_ctxt, frame_pointer->type));
				frame_pointer->mpc = restart_pc;
				frame_pointer->ctxt = restart_ctxt;
			}
			err_act = NULL;
			dollar_ecode.error_last_ecode = SIGNAL;
			if (std_dev_outbnd && io_std_device.in && (tt == io_std_device.in->type)
			    && io_std_device.in->error_handler.len)
			{
				proc_act_type = SFT_DEV_ACT;
				err_act = &io_std_device.in->error_handler;
			} else if (!std_dev_outbnd && err_dev && (tt == err_dev->type) && err_dev->error_handler.len)
			{
				proc_act_type = SFT_DEV_ACT;
				err_act = &err_dev->error_handler;
			} else if (NULL != error_frame)
			{	/* a primary error occurred already. irrespective of whether ZTRAP or ETRAP is active now,
				 * we need to consider this as a nested error and trigger nested error processing.
				 */
				goerrorframe();	/* unwind back to error_frame */
				proc_act_type = 0;
			} else if ((0 != (TREF(dollar_etrap)).str.len) || (0 != (TREF(dollar_ztrap)).str.len))
			{
				assert(!ztrap_explicit_null);
				proc_act_type = SFT_ZTRAP;
				err_act = (0 != (TREF(dollar_etrap)).str.len)
					? &((TREF(dollar_etrap)).str) : &((TREF(dollar_ztrap)).str);
			} else
			{	/* Either $ETRAP is empty-string or ztrap_explicit_null is set
				 *
				 * If ztrap_explicit_null is FALSE
				 *  - Use empty-string $ETRAP for error-handling
				 *
				 * If ztrap_explicit_null is TRUE
				 *  - unwind as many frames as possible until we see a frame where ztrap_explicit_null is
				 *    FALSE and $ZTRAP is not NULL (i.e. we find a $ETRAP handler). In that frame, use $ETRAP
				 *    for error-handling. If no such frame is found, exit after printing the error.
				 */
				etrap_handling = TRUE;
				if (ztrap_explicit_null)
				{
					assert(0 == (TREF(dollar_etrap)).str.len);
					for (level = dollar_zlevel() - 1; level > 0; level--)
					{
						GOLEVEL(level, FALSE);
						assert(level == dollar_zlevel());
						if (ETRAP_IN_EFFECT)
							break;
					}
					if (0 >= level)
					{
						assert(0 == level);
						etrap_handling = FALSE;
						DBGEHND((stderr, "mdb_condition_handler: Unwound to stack start - exiting\n"));
					}
					/* Note that trans_code will set error_frame appropriately for this condition */
				}
				if (SFT_CI & frame_pointer->type)
				{ 	/* Unhandled errors from called-in routines should return to ydb_ci() with error status */
					mumps_status = SIGNAL;
					MUM_TSTART_FRAME_CHECK;
					MUM_TSTART;
				} else if (etrap_handling)
				{
					proc_act_type = SFT_ZTRAP;
					err_act = &((TREF(dollar_etrap)).str);
				} else
				{
					PRN_ERROR;
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_NOEXCNOZTRAP);
				}
			}
			if (clean_mum_tstart())
			{
				MUM_TSTART_FRAME_CHECK;
				MUM_TSTART;
			}
		} else  if (frame_pointer->type & SFT_DM)
		{
			frame_pointer->ctxt = GTM_CONTEXT(call_dm);
			frame_pointer->mpc = CODE_ADDRESS(call_dm);
			frame_pointer->flags &= SSF_NORET_VIA_MUMTSTART_OFF;	/* Frame enterable now with mpc reset */
			GTMTRIG_ONLY(DBGTRIGR((stderr, "mdb_condition_handler: disabling SSF_NORET_VIA_MUMTSTART (2) in frame 0x"
					       lvaddr"\n", frame_pointer)));
		} else
		{
			/* Do cleanup on indirect frames prior to reset */
			IF_INDR_FRAME_CLEANUP_CACHE_ENTRY_AND_UNMARK(frame_pointer);
			frame_pointer->ctxt = GTM_CONTEXT(pseudo_ret);
			frame_pointer->mpc = CODE_ADDRESS(pseudo_ret);
			frame_pointer->flags &= SSF_NORET_VIA_MUMTSTART_OFF;	/* Frame enterable now with mpc reset */
			GTMTRIG_ONLY(DBGTRIGR((stderr, "mdb_condition_handler: disabling SSF_NORET_VIA_MUMTSTART (3) in frame 0x"
					       lvaddr"\n", frame_pointer)));
		}
		PRN_ERROR;
		if (io_curr_device.out != io_std_device.out)
		{
			dec_err(VARLSTCNT(4) ERR_NOTPRINCIO, 2, io_curr_device.out->trans_name->len,
				io_curr_device.out->trans_name->dollar_io);
		}
		MUM_TSTART_FRAME_CHECK;
		MUM_TSTART;
	} else  if ((int)ERR_JOBINTRRQST == SIGNAL)
	{	/* Verify not indirect or that context is unchanged before reset context */
		assert(NULL != restart_pc);
		assert((!(SFF_INDCE & frame_pointer->flags)) || (restart_ctxt == frame_pointer->ctxt));
		DBGEHND((stderr, "mdb_condition_handler(4): Resetting frame 0x"lvaddr" mpc/context with restart_pc/ctxt "
			 "0x"lvaddr"/0x"lvaddr" - frame has type 0x%04lx\n", frame_pointer, restart_pc, restart_ctxt,
			 frame_pointer->type));
		frame_pointer->mpc = restart_pc;
		frame_pointer->ctxt = restart_ctxt;
		assert(!dollar_zininterrupt);
		dollar_zininterrupt = TRUE;	/* Note done before outofband is cleared to prevent nesting */
		outofband_clear();
		proc_act_type = SFT_ZINTR | SFT_COUNT;	/* trans_code will invoke jobinterrupt_process for us */
		MUM_TSTART;
	} else  if ((int)ERR_JOBINTRRETHROW == SIGNAL)
	{ 	/* Job interrupt is rethrown from TC/TRO */
		assert(!dollar_zininterrupt);
		dollar_zininterrupt = TRUE;
		proc_act_type = SFT_ZINTR | SFT_COUNT; /* trans_code will invoke jobinterrupt_process for us */
		MUM_TSTART;
	} else  if ((int)ERR_STACKCRIT == SIGNAL)
	{
		assert(msp > stacktop);
		assert(stackwarn > stacktop);
		cp = stackwarn;
		stackwarn = stacktop;
		push_stck(cp, 0, (void **)&stackwarn, MVST_STCK_SP);
	}
	if (!repeat_error)
		dollar_ecode.error_last_b_line = NULL;
	/* Error from direct mode actions does not set $ZSTATUS and is not restarted (dollar_ecode.error_last_b_line = NULL);
	 * Error from transcendental code does set $ZSTATUS but does not restart the line.
	 */
	if (!dm_action)
	{
		if (!repeat_error)
		{
			SAVE_ZSTATUS_INTO_RTS_STRINGPOOL(dollar_ecode.error_last_b_line, &context);
			assert(NULL != dollar_ecode.error_last_b_line);
		}
	}
	if ((SUCCESS == SEVERITY) || (INFO == SEVERITY))
	{
		/* Send out messages only in utilities or direct mode */
		if (!IS_GTM_IMAGE || dm_action)
			PRN_ERROR;
		CONTINUE;
	}
	/* Error from direct mode actions or "transcendental" code does not invoke MUMPS error handling routines */
	if (!dm_action && !trans_action)
	{
		DBGEHND((stderr, "mdb_condition_handler: Handler to dispatch selection checks\n"));
		err_act = NULL;
		dollar_ecode.error_last_ecode = SIGNAL;
		reset_mpc = FALSE;
		if (err_dev && err_dev->error_handler.len && ((int)ERR_TPTIMEOUT != SIGNAL))
		{
			proc_act_type = SFT_DEV_ACT;
			err_act = &err_dev->error_handler;
			reserve_sock_dev = TRUE;
			/* Reset mpc to beginning of the current line (to retry after processing the IO exception handler) */
			reset_mpc = TRUE;
			DBGEHND((stderr, "mdb_condition_handler: dispatching device error handler [%.*s]\n", err_act->len,
				 err_act->addr));
		} else if (NULL != error_frame)
		{	/* A primary error occurred already. irrespective of whether ZTRAP or ETRAP is active now, we need to
			 * consider this as a nested error and trigger nested error processing.
			 */
			goerrorframe();	/* unwind upto error_frame */
			proc_act_type = 0;
			DBGEHND((stderr, "mdb_condition_handler: Have unwound to error frame via goerrorframe() and am "
				 "re-dispatching error frame\n"));
			MUM_TSTART_FRAME_CHECK;
			MUM_TSTART;	/* unwind the current C-stack and restart executing from the top of the current M-stack */
		} else if ((0 != (TREF(dollar_etrap)).str.len) || (0 != (TREF(dollar_ztrap)).str.len))
		{
			assert(!ztrap_explicit_null);
			proc_act_type = SFT_ZTRAP;
			err_act = (0 != (TREF(dollar_etrap)).str.len) ? &((TREF(dollar_etrap)).str) : &((TREF(dollar_ztrap)).str);
			DBGEHND((stderr, "mdb_condition_handler: Dispatching %s error handler [%.*s]\n",
				 (0 != (TREF(dollar_etrap)).str.len) ? "$ETRAP" : "$ZTRAP", err_act->len, err_act->addr));
			/* Reset mpc to beginning of the current line (to retry after invoking $ZTRAP) */
			if (0 != (TREF(dollar_ztrap)).str.len)
				reset_mpc = TRUE;
		} else
		{	/* Either $ETRAP is empty string or ztrap_explicit_null is set.
			 *
			 * If ztrap_explicit_null is FALSE
			 *   - Use empty-string $ETRAP for error-handling
			 *
			 * If ztrap_explicit_null is TRUE
			 *   - Unwind as many frames as possible until we see a frame where ztrap_explicit_null is FALSE and
			 *     $ZTRAP is *not* NULL (i.e. we found an $ETRAP). In that frame, use $ETRAP for error-handling.
			 *     If no such frame is found, exit after printing the error.
			 */
			etrap_handling = TRUE;
			if (ztrap_explicit_null)
			{
				GTMTRIG_ONLY(assert(0 == gtm_trigger_depth));	/* Should never happen in a trigger */
				DBGEHND((stderr, "mdb_condition_handler: ztrap_explicit_null set - unwinding till find handler\n"));
				assert(0 == (TREF(dollar_etrap)).str.len);
				for (level = dollar_zlevel() - 1; 0 <= level; level--)
				{
					GOLEVEL(level, FALSE);
					assert(level == dollar_zlevel());
					if (ETRAP_IN_EFFECT)
						/* Break if found an $ETRAP covered frame */
						break;
				}
				if (0 >= level)
				{
					etrap_handling = FALSE;
					DBGEHND((stderr, "mdb_condition_handler: Unwound to stack start - exiting\n"));
				}
				/* note that trans_code will set error_frame appropriately for this condition */
			}
			if (SFT_CI & frame_pointer->type)
			{ 	/* Unhandled errors from called-in routines should return to ydb_ci() with error status */
				mumps_status = SIGNAL;
				DBGEHND((stderr, "mdb_condition_handler: Call in base frame found - returning to callins\n"));
				MUM_TSTART_FRAME_CHECK;
				MUM_TSTART;
			} else if (etrap_handling)
			{
				proc_act_type = SFT_ZTRAP;
				err_act = &((TREF(dollar_etrap)).str);
				DBGEHND((stderr, "mdb_condition_handler: $ETRAP handler being dispatched [%.*s]\n", err_act->len,
					 err_act->addr));
			}
		}
		/* if the err_act points to the address err_dev->errro_handler, we cannot destroy socket here */
		if (err_dev && (gtmsocket == err_dev->type) && err_dev->newly_created && !reserve_sock_dev)
		{
			assert(err_dev->state != dev_open);
			iosocket_destroy(err_dev);
			err_dev = NULL;
		}
		if (reset_mpc)
		{	/* Reset the mpc such that
			 *   (a) If the current frame is a counted frame, the error line is retried after the error is handled,
			 *   (b) If the current frame is "transcendental" code, set frame to return.
			 *
			 * If we are in $ZYERROR, we don't care about restarting the line that errored since we will
			 * unwind all frames upto and including zyerr_frame.
			 *
			 * If this is a rethrown error (ERR_REPEATERROR) from a child frame, do NOT reset mpc of the current
			 * frame in that case. We do NOT want to retry the current line (after the error has been
			 * processed) because the error did not occur in this line and therefore re-executing the same
			 * line could cause undesirable effects at the M-user level. We will resume normal execution
			 * once the error is handled. Not that it matters, but note that in the case of a rethrown error
			 * (repeat_error is TRUE), we would NOT have noted down dollar_ecode.error_last_b_line so cannot
			 * use that to reset mpc anyways.
			 */
			if ((NULL == zyerr_frame) && !repeat_error)
			{
				DBGEHND((stderr, "mdb_condition_handler: reset_mpc triggered\n"));
				for (fp = frame_pointer; fp; fp = fp->old_frame_pointer)
				{	/* See if this is a $ZINTERRUPT frame. If yes, we want to restart *this* line
					 * at the beginning. Since it is always an indirect frame, we can use the context
					 * pointer to start over. $ETRAP does things somewhat differently in that the current
					 * frame is always returned from.
					 */
					if (SFT_ZINTR & fp->type)
					{
						assert(SFF_INDCE & fp->flags);
						fp->mpc = fp->ctxt;
						fp->flags &= SSF_NORET_VIA_MUMTSTART_OFF; /* Frame enterable now with mpc reset */
						GTMTRIG_ONLY(
							DBGTRIGR((stderr, "mdb_condition_handler: disabling SSF_NORET_VIA_MUMTSTART"
								  " (4) in frame 0x"lvaddr"\n", frame_pointer)));
						break;
					}
					/* Do cleanup on indirect frames prior to reset */
					IF_INDR_FRAME_CLEANUP_CACHE_ENTRY_AND_UNMARK(fp);
					/* mpc points to PTEXT */
					if (ADDR_IN_CODE(fp->mpc, fp->rvector))
					{	/* GT.M specific error trapping retries the line with the error */
						fp->mpc = dollar_ecode.error_last_b_line;
						fp->ctxt = context;
						fp->flags &= SSF_NORET_VIA_MUMTSTART_OFF; /* Frame enterable now with mpc reset */
						GTMTRIG_ONLY(
							DBGTRIGR((stderr, "mdb_condition_handler: disabling SSF_NORET_VIA_MUMTSTART"
								  " (5) in frame 0x"lvaddr"\n", frame_pointer)));
						break;
					} else
					{
						fp->ctxt = GTM_CONTEXT(pseudo_ret);
						fp->mpc = CODE_ADDRESS(pseudo_ret);
						fp->flags &= SSF_NORET_VIA_MUMTSTART_OFF; /* Frame enterable now with mpc reset */
						GTMTRIG_ONLY(
							DBGTRIGR((stderr, "mdb_condition_handler: disabling SSF_NORET_VIA_MUMTSTART"
							  " (6) in frame 0x"lvaddr"\n", frame_pointer)));
					}
				}
			}
		}
		if (clean_mum_tstart())
		{
			if (err_dev)
			{
				/* On z/OS, if opening a fifo which is not read only we need to fix the err_dev type to rm */
#				ifdef __MVS__
				if ((dev_open != err_dev->state) && (ff == err_dev->type))
				{
					assert(NULL != err_dev->pair.out);
					if (rm == err_dev->pair.out->type)
					{
						/* Have to massage the device so remove_rms will cleanup the partially
						 * created fifo.  Refer to io_open_try.c for creation of split fifo device.
						 */
						err_dev->newly_created = 1;
						err_dev->type = rm;
						err_dev->dev_sp = err_dev->pair.out->dev_sp;
						err_dev->pair.out->dev_sp = NULL;
					}
				}
#				endif
				if ((dev_open != err_dev->state) && (rm == err_dev->type))
				{
					gtm_err_dev = err_dev;
					/* structures pointed to by err_dev were freed so make sure it's not used again */
					err_dev = NULL;
				}
				if (err_dev && (gtmsocket == err_dev->type) && err_dev->newly_created)
				{
					assert(err_dev->state != dev_open);
					assert(reserve_sock_dev);
					gtm_err_dev = err_dev;
					err_dev = NULL;
				}
				if (err_dev && (n_io_dev_types == err_dev->type))
				{
					/* Got some error while opening the device. Clean up the structures. */
					gtm_err_dev = err_dev;
					err_dev = NULL;
				}
			}
			MUM_TSTART_FRAME_CHECK;
			MUM_TSTART;
		} else
		{
			/* err_act is null, we can remove the socket device */
			if (err_dev && (gtmsocket == err_dev->type) && err_dev->newly_created && !reserve_sock_dev)
			{
				assert(err_act == NULL);
				assert(err_dev->state != dev_open);
				iosocket_destroy(err_dev);
				err_dev = NULL;
			}
			DBGEHND((stderr, "mdb_condition_handler: clean_mum_tstart returned FALSE\n"));
		}
	} else
	{
		DBGEHND((stderr, "mdb_condition_handler: Transient or direct mode frame -- bypassing handler dispatch\n"));
		if (err_dev)
		{
			/* Executed from the direct mode so do the rms check and cleanup if necessary. On z/OS, if opening a fifo
			 * which is not read only we need to fix the type for the err_dev to rm.
			 */
#			ifdef __MVS__
			if ((dev_open != err_dev->state) && (ff == err_dev->type))
			{
				assert(NULL != err_dev->pair.out);
				if (rm == err_dev->pair.out->type)
				{
					/* Have to massage the device so remove_rms will cleanup the partially created fifo */
					err_dev->newly_created = 1;
					err_dev->type = rm;
					err_dev->dev_sp = err_dev->pair.out->dev_sp;
					err_dev->pair.out->dev_sp = NULL;
				}
			}
#			endif
			if (((dev_open != err_dev->state) && (rm == err_dev->type)) || (n_io_dev_types == err_dev->type))
			{
				remove_rms(err_dev);
				err_dev = NULL;
			}
			if (err_dev && (gtmsocket == err_dev->type) && err_dev->newly_created)
			{
				assert(err_dev->state != dev_open);
				iosocket_destroy(err_dev);
				err_dev = NULL;
			}
		}
	}
	if (((SFT_ZINTR | SFT_COUNT) != proc_act_type) || (0 == dollar_ecode.error_last_b_line))
	{	/* No user console error for $zinterrupt compile problems and if not direct mode. Accomplish
		 * this by bypassing the code inside this if which *will* be executed for most cases
		 */
		DBGEHND((stderr, "mdb_condition_handler: Printing error status\n"));
		/* If we have a transcendental frame, we won't have driven an error handler but neither do we want to
		 * push this error out at this point. The call to trans_code_cleanup() below will null this frame and
		 * any other up to the next counted frame out by changing their mpc to pseudo_ret and will rethrow the
		 * error we have here so there is no need to push this error out here for anything but direct mode.
		 */
		UNIX_ONLY(if (dm_action))
		{
			PRN_ERROR;
			if (TREF(compile_time) && (((int)ERR_LABELMISSING) != SIGNAL))
				show_source_line(TRUE);
		}
	}
	/* We now have a strict no-unsolicited output from error messages policy unless dealing with a direct mode frame.
	 * This has a dependency on a robust error_return(). Consequently, a new cleaner (from an unsolicited console output)
	 * viewpoint is in place.
	 */
	if (trans_action || dm_action)
	{	/* If true transcendental, do trans_code_cleanup(). If our counted frame is
		 * masquerading as a transcendental frame, run jobinterrupt_process_cleanup().
		 */
		DBGEHND((stderr, "mdb_condition_handler: trans_code_cleanup() or jobinterrupt_process_cleanup being "
			 "dispatched\n"));
		if (!(SFT_ZINTR & proc_act_type))
			trans_code_cleanup();
		else
			jobinterrupt_process_cleanup();
		MUM_TSTART_FRAME_CHECK;
		MUM_TSTART;
	}
	DBGEHND((stderr, "mdb_condition_handler: Condition not handled -- defaulting to process exit\n"));
	assert(!(MUMPS_CALLIN & invocation_mode));
	MUMPS_EXIT;
}
