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

#include "gtm_string.h"
#include "gtm_unistd.h"

#include "lv_val.h"
#include "rtnhdr.h"
#include "error.h"
#include "mv_stent.h"
#include "find_mvstent.h"	/* for zintcmd_active */
#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "hashtab_int4.h"
#include "gdskill.h"
#include "jnl.h"
#include "gdscc.h"
#include "buddy_list.h"
#include "tp.h"
#include "gvname_info.h"
#include "op_merge.h"
#include "zwrite.h"
#include "zshow.h"
#include "dpgbldir.h"
#include "dpgbldir_sysops.h"
#include "error_trap.h"		/* for STACK_ZTRAP_EXPLICIT_NULL macro */
#include "op.h"
#include "gt_timer.h"
#include "iosocketdef.h"
#include "have_crit.h"
#include "iormdef.h"
#include "iottdef.h"
#include "stack_frame.h"
#include "alias.h"
#include "tp_timeout.h"
#include "localvarmonitor.h"
#include "gtmio.h"
#ifdef GTM_TRIGGER
#include "gv_trigger.h"
#include "gtm_trigger.h"
#endif

GBLREF symval			*curr_symval;
GBLREF boolean_t		dollar_truth;
GBLREF gv_key			*gv_currkey;
GBLREF gv_namehead		*gv_target;
GBLREF gd_addr			*gd_header;
GBLREF dollar_ecode_type	dollar_ecode;
GBLREF dollar_stack_type	dollar_stack;
GBLREF mval			dollar_zgbldir;
GBLREF volatile boolean_t	dollar_zininterrupt;
GBLREF boolean_t		ztrap_explicit_null;		/* whether $ZTRAP was explicitly set to NULL in this frame */
GBLREF mstr			extnam_str;
GBLREF stack_frame		*frame_pointer, *error_frame;
GBLREF lv_xnew_var		*xnewvar_anchor;
#ifdef GTM_TRIGGER
GBLREF mstr			*dollar_ztname;
GBLREF mval			*dollar_ztdata;
GBLREF mval			*dollar_ztdelim;
GBLREF mval			*dollar_ztoldval;
GBLREF mval			*dollar_ztriggerop;
GBLREF mval			*dollar_ztupdate;
GBLREF mval			*dollar_ztvalue;
GBLREF boolean_t		*ztvalue_changed_ptr;
GBLREF int			mumps_status;
GBLREF boolean_t		run_time;
GBLREF int4			gtm_trigger_depth;
GBLREF symval			*trigr_symval_list;
#  ifdef DEBUG
GBLREF gv_trigger_t		*gtm_trigdsc_last;		/* For debugging purposes - parms gtm_trigger called with */
GBLREF gtm_trigger_parms	*gtm_trigprm_last;
GBLREF ch_ret_type		(*ch_at_trigger_init)();
#  endif
#endif
GBLREF unsigned char		*restart_pc, *restart_ctxt;
GBLREF mval			*alias_retarg;
GBLREF int			merge_args;
GBLREF uint4			zwrtacindx;
GBLREF merge_glvn_ptr		mglvnp;
GBLREF gvzwrite_datablk		*gvzwrite_block;
GBLREF lvzwrite_datablk		*lvzwrite_block;
GBLREF zshow_out		*zwr_output;
GBLREF zwr_hash_table		*zwrhtab;
GBLREF boolean_t		tp_timeout_deferred;

#define FREEIFALLOC(ADR) if (NULL != (ADR)) free(ADR)

void unw_mv_ent(mv_stent *mv_st_ent)
{
	lv_blk			*lp, *lpnext;
	lv_val			*lvval_ptr;
	symval			*symval_ptr, *sym;
	ht_ent_mname		*hte;
	lv_xnew_var		*xnewvar, *xnewvarnext;
	d_socket_struct		*dsocketptr;
	lvzwrite_datablk        *zwrblk, *prevzwrblk;
	zwr_zav_blk		*zavb, *zavb_next;
	UNIX_ONLY(d_rm_struct	*rm_ptr;)
	socket_interrupt 	*sockintr;
	socket_struct		*socketptr;
	zintcmd_ops		zintcmd_command;
	intrpt_state_t		prev_intrpt_state;
	UNIX_ONLY(d_tt_struct	*tt_ptr;)
	DBGRFCT_ONLY(mident_fixed vname;)
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	SET_ACTIVE_LV(NULL, TRUE, actlv_unw_mv_ent);	/* If we get here, subscript set was successful.
							 * Clear active_lv to avoid later cleanup issues */
	switch (mv_st_ent->mv_st_type)
	{
		case MVST_MSAV:
			*mv_st_ent->mv_st_cont.mvs_msav.addr = mv_st_ent->mv_st_cont.mvs_msav.v;
			if (&(TREF(dollar_etrap)) == mv_st_ent->mv_st_cont.mvs_msav.addr)
			{
				ztrap_explicit_null = FALSE;
				(TREF(dollar_ztrap)).str.len = 0;
			} else if (&(TREF(dollar_ztrap)) == mv_st_ent->mv_st_cont.mvs_msav.addr)
			{
				if (STACK_ZTRAP_EXPLICIT_NULL == (TREF(dollar_ztrap)).str.len)
				{
					(TREF(dollar_ztrap)).str.len = 0;
					ztrap_explicit_null = TRUE;
				} else
					ztrap_explicit_null = FALSE;
				(TREF(dollar_etrap)).str.len = 0;
				if (tp_timeout_deferred UNIX_ONLY( && !dollar_zininterrupt))
					/* A tp timeout was deferred. Now that $ETRAP is no longer in effect and we are not in a
					 * job interrupt, the timeout can no longer be deferred and needs to be recognized.
					 */
					tptimeout_set(0);
			} else if (mv_st_ent->mv_st_cont.mvs_msav.addr == &dollar_zgbldir)
			{
				/* Restore GLD if a match is found, otherwise defer setting gd_header */
				gd_header = zgbldir_name_lookup_only(&dollar_zgbldir);
				if (gv_currkey)
				{
					gv_currkey->base[0] = 0;
					gv_currkey->prev = gv_currkey->end = 0;
				}
				if (gv_target)
					gv_target->clue.end = 0;
			}
			return;
		case MVST_MVAL:
		case MVST_IARR:
		case MVST_TPHOLD:
		case MVST_STORIG:
			return;
		case MVST_LVAL:
			/* Reduce the reference count of this unanchored lv_val (current usage as callin argument
			 * holder) and put on free list if no longer in use.
			 */
			lvval_ptr = mv_st_ent->mv_st_cont.mvs_lvval;
			DECR_BASE_REF_NOSYM(lvval_ptr, FALSE);
			return;
		case MVST_STAB:
			if (mv_st_ent->mv_st_cont.mvs_stab)
			{
				assert(mv_st_ent->mv_st_cont.mvs_stab == curr_symval);
				symval_ptr = curr_symval;
				curr_symval = symval_ptr->last_tab;
				(TREF(curr_symval_cycle))++;
				DBGRFCT((stderr, "\n\n***** unw_mv_ent-STAB: ** Symtab pop with 0x"lvaddr" replacing 0x"
					 lvaddr"\n\n", curr_symval, symval_ptr));
#				ifdef GTM_TRIGGER
				/* If this is a trigger symval, don't fully unwind it but rather put it on the trigr_symval_list
				 * so it can be reused without having to be fully recreated again.
				 */
				if (symval_ptr->trigr_symval)
				{	/* Relist it */
					assert(NULL == symval_ptr->xnew_var_list);
					assert(NULL == symval_ptr->xnew_ref_list);
					symval_ptr->last_tab = trigr_symval_list;
					trigr_symval_list = symval_ptr;
					/* Note we do not set SFF_UNW_SYMVAL here because this being a trigger related symbol
					 * table, when it unwinds, so has any possible reference to what was using it.
					 */
					return;
				}
#				endif
				if (symval_ptr->alias_activity && ((NULL != symval_ptr->xnew_var_list) || (NULL != alias_retarg)))
					/* Special cleanup for aliases and passed through vars */
					als_check_xnew_var_aliases(symval_ptr, curr_symval);
				else
				{	/* Drop reference counts & requeue any lv_xnew_var blocks we have */
					assert(NULL == symval_ptr->xnew_ref_list);	/* Without aliases, no ref list possible */
					for (xnewvar = symval_ptr->xnew_var_list; xnewvar; xnewvar = xnewvarnext)
					{
						hte = lookup_hashtab_mname(&curr_symval->h_symtab, &xnewvar->key);
						lvval_ptr = (lv_val *)hte->value;
						assert(lvval_ptr);
						DECR_CREFCNT(lvval_ptr);
						assert(1 <= lvval_ptr->stats.trefcnt);
						DECR_BASE_REF_NOSYM(lvval_ptr, TRUE);
						xnewvarnext = xnewvar->next;
						xnewvar->next = xnewvar_anchor;
						xnewvar_anchor = xnewvar;
					}
				}
				for (lp = symval_ptr->lv_first_block; NULL != lp; lp = lpnext)
				{
					lpnext = lp->next;
					free(lp);
				}
				symval_ptr->lv_first_block = NULL;
				for (lp = symval_ptr->lvtree_first_block; NULL != lp; lp = lpnext)
				{
					lpnext = lp->next;
					free(lp);
				}
				symval_ptr->lvtree_first_block = NULL;
				for (lp = symval_ptr->lvtreenode_first_block; NULL != lp; lp = lpnext)
				{
					lpnext = lp->next;
					free(lp);
				}
				symval_ptr->lvtreenode_first_block = NULL;
				free_hashtab_mname(&symval_ptr->h_symtab);
				free(symval_ptr);
				frame_pointer->flags |= SFF_UNW_SYMVAL;
			}
			return;
		case MVST_NTAB:
			DEBUG_ONLY(hte = lookup_hashtab_mname(&curr_symval->h_symtab, mv_st_ent->mv_st_cont.mvs_ntab.nam_addr));
			assert(hte);
			assert(hte == mv_st_ent->mv_st_cont.mvs_ntab.hte_addr);
			hte = mv_st_ent->mv_st_cont.mvs_ntab.hte_addr;
			lvval_ptr = (lv_val *)hte->value;
			DBGRFCT_ONLY(
				memcpy(vname.c, hte->key.var_name.addr, hte->key.var_name.len);
				vname.c[hte->key.var_name.len] = '\0';
			);
			DBGRFCT((stderr, "unw_mv_ent-NTAB: var '%s' hte 0x"lvaddr" being reset from 0x"lvaddr" to 0x"lvaddr"\n",
				 &vname.c, hte, hte->value, mv_st_ent->mv_st_cont.mvs_ntab.save_value));
			hte->value = (char *)mv_st_ent->mv_st_cont.mvs_ntab.save_value;
			if (lvval_ptr)
			{
				DECR_BASE_REF_NOSYM(lvval_ptr, FALSE);
			}
			return;
		case MVST_PVAL:
			if (mv_st_ent->mv_st_cont.mvs_pval.mvs_ptab.hte_addr)
			{
				assert(mv_st_ent->mv_st_cont.mvs_pval.mvs_ptab.nam_addr);
				DEBUG_ONLY(hte = lookup_hashtab_mname(&curr_symval->h_symtab,
								      mv_st_ent->mv_st_cont.mvs_pval.mvs_ptab.nam_addr));
				assert(hte);
				assert(hte == mv_st_ent->mv_st_cont.mvs_pval.mvs_ptab.hte_addr);
				hte = mv_st_ent->mv_st_cont.mvs_pval.mvs_ptab.hte_addr;
				lvval_ptr = (lv_val *)hte->value;	/* lv_val being popped */
				DBGRFCT_ONLY(
					memcpy(vname.c, hte->key.var_name.addr, hte->key.var_name.len);
					vname.c[hte->key.var_name.len] = '\0';
				);
				DBGRFCT((stderr, "unw_mv_ent-PVAL: var '%s' hte 0x"lvaddr" being reset from 0x"lvaddr" to 0x"lvaddr
					 "\n", &vname.c, hte, hte->value, mv_st_ent->mv_st_cont.mvs_pval.mvs_ptab.save_value));
				hte->value = (char *)mv_st_ent->mv_st_cont.mvs_pval.mvs_ptab.save_value;
				/* At this point lvval_ptr has one of two values:
				 * 1 - It has the same value as in mv_st_ent->mv_st_cont.mvs_pval.mvs_val which is the lv_val
				 *     allocated for use for the duration of this mv_stent (replaced the lv_val saved in
				 *     our save_value field which was restored just above). This is the normal case when
				 *     either this var is not aliased or if it was the source of the alias.
				 * 2 - Say this was variable "A" and someone did a "SET *A=B". In that case, the hashtable entry
				 *     would be pointed to "B" and this temporary lv_val would be already returned to the free
				 *     queue and we no longer need to worry about it. In this case lvval would not be the
				 *     same address as the temp mval yet the same operations need to be done on it. Note that
				 *     an lvval_ptr value of NULL is just a special version of this case since the hashtable
				 *     value would not have changed except for alias processing that freed the original lv_val.
				 * So we end up with two possible types of addrs but we handle them exactly the same - namely
				 * decrement their reference counts and if they have gone to zero meaning nothing else points to
				 * them, then delete them and requeue them.
				 * There really isn't an assert we can put here to validate these conditions since in either case
				 * our var could have become an alias and been "unaliased" many times over.
				 */
				if (lvval_ptr)
					DECR_BASE_REF_NOSYM(lvval_ptr, FALSE);
				return;
			}
			/* We could use the LV_FREESLOT macro here but we already know which symbol-table we need to use and
			 * hence avoid that step by directly invoking the LV_FLIST_ENQUEUE macro instead. We however ensure
			 * that the bypass is valid by asserting that the symbol table for the lv is indeed what we think it is.
			 */
			assert(curr_symval == LV_GET_SYMVAL(mv_st_ent->mv_st_cont.mvs_pval.mvs_val));
			LV_FLIST_ENQUEUE(&curr_symval->lv_flist, mv_st_ent->mv_st_cont.mvs_pval.mvs_val);
			return;
		case MVST_NVAL:
			assert(mv_st_ent->mv_st_cont.mvs_nval.mvs_ptab.hte_addr);
			DEBUG_ONLY(hte = lookup_hashtab_mname(&curr_symval->h_symtab, &mv_st_ent->mv_st_cont.mvs_nval.name));
			assert(hte);
			assert(hte == mv_st_ent->mv_st_cont.mvs_nval.mvs_ptab.hte_addr);
			hte = mv_st_ent->mv_st_cont.mvs_nval.mvs_ptab.hte_addr;
			lvval_ptr = (lv_val *)hte->value;	/* lv_val being popped */
			DBGRFCT_ONLY(
				memcpy(vname.c, hte->key.var_name.addr, hte->key.var_name.len);
				vname.c[hte->key.var_name.len] = '\0';
			);
			DBGRFCT((stderr, "unw_mv_ent-NVAL: var '%s' hte 0x"lvaddr" being reset from 0x"lvaddr" to 0x"lvaddr"\n",
				 &vname.c, hte, hte->value, mv_st_ent->mv_st_cont.mvs_nval.mvs_ptab.save_value));
			hte->value = (char *)mv_st_ent->mv_st_cont.mvs_nval.mvs_ptab.save_value;
			/* See comment in handling of MVST_PVAL above for content and treatment of lvval_ptr */
			if (lvval_ptr)
			{
				DECR_BASE_REF_NOSYM(lvval_ptr, FALSE);
			}
			return;
		case MVST_STCK:
		case MVST_STCK_SP:
			assert(mvs_size[MVST_STCK] == mvs_size[MVST_STCK_SP]);
			if (0 < mv_st_ent->mv_st_cont.mvs_stck.mvs_stck_size)
				memcpy(*(mv_st_ent->mv_st_cont.mvs_stck.mvs_stck_addr), (char*)mv_st_ent + mvs_size[MVST_STCK],
				       mv_st_ent->mv_st_cont.mvs_stck.mvs_stck_size);
			else
				*(mv_st_ent->mv_st_cont.mvs_stck.mvs_stck_addr) =
					(unsigned char *)mv_st_ent->mv_st_cont.mvs_stck.mvs_stck_val;
			return;
		case MVST_TVAL:
			dollar_truth = (boolean_t)mv_st_ent->mv_st_cont.mvs_tval;
			return;
		case MVST_ZINTDEV:
			/* Since the interrupted device frame is popping off, there is no way that the READ
			 * that was interrupted will be resumed (if it already hasn't been). We don't bother
			 * to check if it is or isn't. We just reset the device.
			 */
			if (NULL == mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr)
				return;	/* already processed */
			switch(mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr->type)
			{
				case tt:
					if (NULL != mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr)
					{	/* This mv_stent has not been processed yet */
						tt_ptr = (d_tt_struct *)(mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr->dev_sp);
						tt_ptr->mupintr = FALSE;
						tt_ptr->tt_state_save.who_saved = ttwhichinvalid;
						mv_st_ent->mv_st_cont.mvs_zintdev.buffer_valid = FALSE;
						mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr = NULL;
					}
					return;
				case rm:
					if (NULL != mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr)
					{	/* This mv_stent has not been processed yet */
						rm_ptr = (d_rm_struct *)(mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr->dev_sp);
						assert(rm_ptr->is_pipe || rm_ptr->fifo || rm_ptr->follow);
						rm_ptr->mupintr = FALSE;
						rm_ptr->pipe_save_state.who_saved = pipewhich_invalid;
						mv_st_ent->mv_st_cont.mvs_zintdev.buffer_valid = FALSE;
						mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr = NULL;
					}
					return;
				case gtmsocket:
					if (NULL != mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr)
					{	/* This mv_stent has not been processed yet */
						dsocketptr = (d_socket_struct *)(mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr->dev_sp);
						sockintr = &dsocketptr->sock_save_state;
						sockintr->end_time_valid = FALSE;
						dsocketptr->mupintr = FALSE;
						if (sockwhich_connect == sockintr->who_saved)
						{	/* clean up socketptr structure */
							socketptr = (socket_struct *)mv_st_ent->mv_st_cont.mvs_zintdev.socketptr;
							if (socket_connect_inprogress == socketptr->state
									&& (FD_INVALID != socketptr->sd))
								close(socketptr->sd);
							if (NULL != socketptr->remote.ai_head)
								freeaddrinfo(socketptr->remote.ai_head);
							if (NULL != socketptr->zff.addr)
								free(socketptr->zff.addr);
							if (NULL != socketptr->buffer)
								free(socketptr->buffer);
							iosocket_delimiter((unsigned char *)NULL, 0, socketptr, TRUE);
							free(socketptr);
						}
						sockintr->who_saved = sockwhich_invalid;
					}
					mv_st_ent->mv_st_cont.mvs_zintdev.buffer_valid = FALSE;
					mv_st_ent->mv_st_cont.mvs_zintdev.io_ptr = NULL;
					return;
				default:
					assertpro(FALSE);	/* No other device should be here */
			}
			break;
		case MVST_ZINTCMD:
			zintcmd_command = mv_st_ent->mv_st_cont.mvs_zintcmd.command;
			assert((0 < zintcmd_command) && (ZINTCMD_LAST > zintcmd_command));
			/* restore previous active interrupted command information */
			TAREF1(zintcmd_active, zintcmd_command).restart_pc_last
				= mv_st_ent->mv_st_cont.mvs_zintcmd.restart_pc_prior;
			TAREF1(zintcmd_active, zintcmd_command).restart_ctxt_last
				= mv_st_ent->mv_st_cont.mvs_zintcmd.restart_ctxt_prior;
			TAREF1(zintcmd_active, zintcmd_command).count--;
			assert(0 <= TAREF1(zintcmd_active, zintcmd_command).count);
			return;
		case MVST_ZINTR:
			/* Restore environment to pre-$zinterrupt evocation. Note the first few elements of MVST_ZINTR
			 * and MVST_TRIGR are the same, so the processing of those elements is commonized.
			 */
			dollar_zininterrupt = FALSE;
			/* Get rid of old values that may exist */
			if (dollar_ecode.begin)
				free(dollar_ecode.begin);
			if (dollar_ecode.array)
				free(dollar_ecode.array);
			if (dollar_stack.begin)
				free(dollar_stack.begin);
			if (dollar_stack.array)
				free(dollar_stack.array);
			/* Restore the old values from dollar_ecode_ci and dollar_stack_ci */
			DBGEHND((stderr, "unw_mv_ent-ZINTR: Restoring saved error frame 0x"lvaddr" over existing error frame value"
				 " 0x"lvaddr"\n", mv_st_ent->mv_st_cont.mvs_zintr.error_frame_save, error_frame));
			error_frame = mv_st_ent->mv_st_cont.mvs_zintr.error_frame_save;
			memcpy(&dollar_ecode, &mv_st_ent->mv_st_cont.mvs_zintr.dollar_ecode_save, SIZEOF(dollar_ecode));
			memcpy(&dollar_stack, &mv_st_ent->mv_st_cont.mvs_zintr.dollar_stack_save, SIZEOF(dollar_stack));
			/* Fall into MVST_TRIGR */
		case MVST_TRIGR:
			dollar_truth = (boolean_t)mv_st_ent->mv_st_cont.mvs_trigr.saved_dollar_truth;
			op_gvrectarg(&mv_st_ent->mv_st_cont.mvs_trigr.savtarg);
			extnam_str.len = mv_st_ent->mv_st_cont.mvs_trigr.savextref.len;
			if (extnam_str.len)
				memcpy(extnam_str.addr, mv_st_ent->mv_st_cont.mvs_trigr.savextref.addr, extnam_str.len);
#			ifdef GTM_TRIGGER
			if (MVST_TRIGR != mv_st_ent->mv_st_type) /* MVST_ZINTR common handling ends here */
				return;
			ztvalue_changed_ptr = mv_st_ent->mv_st_cont.mvs_trigr.ztvalue_changed_ptr;
			dollar_ztvalue = mv_st_ent->mv_st_cont.mvs_trigr.ztvalue_save;
			dollar_ztname = mv_st_ent->mv_st_cont.mvs_trigr.ztname_save;
			dollar_ztdata = mv_st_ent->mv_st_cont.mvs_trigr.ztdata_save;
			dollar_ztdelim = mv_st_ent->mv_st_cont.mvs_trigr.ztdelim_save;
			dollar_ztoldval = mv_st_ent->mv_st_cont.mvs_trigr.ztoldval_save;
			dollar_ztriggerop = mv_st_ent->mv_st_cont.mvs_trigr.ztriggerop_save;
			dollar_ztupdate = mv_st_ent->mv_st_cont.mvs_trigr.ztupdate_save;
			mumps_status = mv_st_ent->mv_st_cont.mvs_trigr.mumps_status_save;
			run_time = mv_st_ent->mv_st_cont.mvs_trigr.run_time_save;
#			ifdef DEBUG
			gtm_trigdsc_last = (gv_trigger_t *)mv_st_ent->mv_st_cont.mvs_trigr.gtm_trigdsc_last_save;
			gtm_trigprm_last = (gtm_trigger_parms *)mv_st_ent->mv_st_cont.mvs_trigr.gtm_trigprm_last_save;
#			endif
			assert(gtm_trigger_depth >= mv_st_ent->mv_st_cont.mvs_trigr.gtm_trigger_depth_save);
			gtm_trigger_depth = mv_st_ent->mv_st_cont.mvs_trigr.gtm_trigger_depth_save;
			if (0 == gtm_trigger_depth)
			{	/* Only restore error handling environment if returning out of trigger-world */
				TREF(dollar_etrap) = mv_st_ent->mv_st_cont.mvs_trigr.dollar_etrap_save;
				TREF(dollar_ztrap) = mv_st_ent->mv_st_cont.mvs_trigr.dollar_ztrap_save;
				ztrap_explicit_null = mv_st_ent->mv_st_cont.mvs_trigr.ztrap_explicit_null_save;
			}
			if (TREF(trig_forced_unwind))
			{	/* This is a forced unwind of the trigger context so reset the condition handler stack too */
				DEFER_INTERRUPTS(INTRPT_IN_CONDSTK, prev_intrpt_state);
				CHECKHIGHBOUND(mv_st_ent->mv_st_cont.mvs_trigr.ctxt_save);
				CHECKLOWBOUND(mv_st_ent->mv_st_cont.mvs_trigr.ctxt_save);
				ctxt = mv_st_ent->mv_st_cont.mvs_trigr.ctxt_save;
				/* same assert as in gtm_trigger.c */
				assert(((0 == gtm_trigger_depth)
						&& ((&ydb_simpleapi_ch == ctxt->ch)
							|| (ch_at_trigger_init == ctxt->ch)
							|| (((ch_at_trigger_init == (ctxt - 1)->ch)
									|| (&ydb_simpleapi_ch == (ctxt - 1)->ch))
								&& ((&gvcst_put_ch == ctxt->ch) || (&gvcst_kill_ch == ctxt->ch)
									|| (&gvcst_spr_kill_ch == ctxt->ch)))))
					|| ((0 < gtm_trigger_depth)
						&& (((&mdb_condition_handler == ctxt->ch)
							|| (((&mdb_condition_handler == (ctxt - 1)->ch)
									|| (&ydb_simpleapi_ch == (ctxt - 1)->ch))
								&& ((&gvcst_put_ch == ctxt->ch) || (&gvcst_kill_ch == ctxt->ch)
									|| (&gvcst_spr_kill_ch == ctxt->ch)))))));
				active_ch = ctxt;
				ctxt->ch_active = FALSE;
				ENABLE_INTERRUPTS(INTRPT_IN_CONDSTK, prev_intrpt_state);
				TREF(trig_forced_unwind) = FALSE;
			}
			if (tp_timeout_deferred && !((0 < dollar_ecode.index) && (ETRAP_IN_EFFECT)) && !dollar_zininterrupt)
			{	/* A tp timeout was deferred. Now that $ETRAP is no longer in effect and/or we are no
				 * longer in a job interrupt, the timeout can no longer be deferred and needs to be
				 * recognized.
				 */
				tp_timeout_deferred = FALSE;
				tptimeout_set(0);
			}
#			endif	/* GTM_TRIGGER */
			return;
		case MVST_RSTRTPC:
			restart_pc = mv_st_ent->mv_st_cont.mvs_rstrtpc.restart_pc_save;
			restart_ctxt = mv_st_ent->mv_st_cont.mvs_rstrtpc.restart_ctxt_save;
			return;
		case MVST_MRGZWRSV:
			/* Note: The below code restores what was saved in PUSH_MVST_MRGZWRSV_IF_NEEDED.
			 *       Ensure the two are kept in sync.
			 */
			merge_args = mv_st_ent->mv_st_cont.mvs_mrgzwrsv.save_merge_args;
			zwrtacindx = mv_st_ent->mv_st_cont.mvs_mrgzwrsv.save_zwrtacindx;
			if (NULL != mglvnp)
			{	/* Release this block and sub-blocks */
				FREEIFALLOC(mglvnp->gblp[0]);
				FREEIFALLOC(mglvnp->gblp[1]);
				free(mglvnp);
			}
			TREF(in_zwrite) = mv_st_ent->mv_st_cont.mvs_mrgzwrsv.save_in_zwrite;
			mglvnp = mv_st_ent->mv_st_cont.mvs_mrgzwrsv.save_mglvnp;
			if (NULL != lvzwrite_block)
			{
				for (zwrblk = lvzwrite_block; zwrblk; zwrblk = prevzwrblk)
				{
					prevzwrblk = zwrblk->prev;
					FREEIFALLOC(zwrblk->sub);
					free(zwrblk);
				}
			}
			lvzwrite_block = mv_st_ent->mv_st_cont.mvs_mrgzwrsv.save_lvzwrite_block;
			if (NULL != gvzwrite_block)
			{
				FREEIFALLOC(gvzwrite_block->sub);
				free(gvzwrite_block);
			}
			gvzwrite_block = mv_st_ent->mv_st_cont.mvs_mrgzwrsv.save_gvzwrite_block;
			zwr_output = mv_st_ent->mv_st_cont.mvs_mrgzwrsv.save_zwr_output;
			if (NULL != zwrhtab)
			{
				FREEIFALLOC(zwrhtab->h_zwrtab.base);
				FREEIFALLOC(zwrhtab->h_zwrtab.spare_base);
				for (zavb = zwrhtab->first_zwrzavb; zavb; zavb = zavb_next)
				{
					zavb_next = zavb->next;
					free(zavb);
				}
				free(zwrhtab);
			}
			zwrhtab = mv_st_ent->mv_st_cont.mvs_mrgzwrsv.save_zwrhtab;
			return;
		default:
			assertpro(mv_st_ent->mv_st_type != mv_st_ent->mv_st_type);
			break;
	}
	return; /* This should never get executed, added to make compiler happy */
}
