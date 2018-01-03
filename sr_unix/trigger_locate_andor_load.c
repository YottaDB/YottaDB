/****************************************************************
 *								*
 * Copyright (c) 2011-2017 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#ifdef GTM_TRIGGER
#include "error.h"
#include "gdsroot.h"			/* for gdsfhead.h */
#include "gdsbt.h"			/* for gdsfhead.h */
#include "gdsfhead.h"
#include "gvcst_protos.h"
#include <rtnhdr.h>
#include "gv_trigger.h"
#include "gtm_trigger.h"
#include "trigger.h"
#include "min_max.h"
#include "filestruct.h"			/* for INITIAL_HASHT_ROOT_SEARCH_IF_NEEDED (FILE_INFO) */
#include "gdscc.h"			/* needed for tp.h */
#include "gdskill.h"			/* needed for tp.h */
#include "buddy_list.h"			/* needed for tp.h */
#include "hashtab_int4.h"		/* needed for tp.h */
#include "jnl.h"			/* needed for tp.h */
#include "tp.h"
#include "op.h"
#include "op_tcommit.h"
#include "tp_frame.h"
#include "gvnh_spanreg.h"
#include "trigger_read_andor_locate.h"
#include "repl_msg.h"			/* for gtmsource.h */
#include "gtmsource.h"			/* for jnlpool_addrs_ptr_t */

GBLREF	sgmnt_addrs		*cs_addrs;
GBLREF	sgmnt_data_ptr_t	cs_data;
GBLREF	gd_addr			*gd_header;
GBLREF	rtn_tabent		*rtn_names_end;
GBLREF	tp_frame		*tp_pointer;
GBLREF	gv_key			*gv_currkey;
GBLREF	gd_region		*gv_cur_region;
GBLREF	sgm_info		*sgm_info_ptr;
GBLREF	jnlpool_addrs_ptr_t	jnlpool;

LITREF	mval	literal_batch;

error_def(ERR_TRIGNAMENF);

/* Routine to locate the named trigger and if not loaded, to go ahead and load it.
 *
 * The following sitations can exist:
 *
 *   1. No trigger by the given name is loaded. For this situation, we need to locate and load the trigger.
 *   2. Trigger is loaded. Note this routine does no validation of the trigger but just returns whatever is
 *      currently loaded.
 *
 * Note, this routine is for locating a trigger that is not to be run and merrily located so its embedded source
 * can be made available. Other trigger related routines are:
 *
 *   a. trigger_source_read_andor_verify() - Verifies has current trigger object (primary usage op_setbrk()).
 *   b. trigger_fill_xecute_buffer() - Same verification but makes sure source is available - typically used when
 *      trigger is about to be driven.
 *
 * Note this routine has similar components to trigger_source_read_andor_verify() and its subroutines so updates
 * to that routine should also be check if they apply here and vice versa. This routine is lighter weight without
 * the overhead of being in a transaction unless it has to load a trigger in which case it calls the full routine
 * trigger_source_read_andor_verify(). Since a trigger's source is embedded in a trigger, we only need to locate
 * the trigger to have access to the source for $TEXT() type services. The primary issue this separate mechanism
 * solves is when an error occurs and trigger is on the M stack, error handling will attempt to locate the source
 * line in the trigger where it was as part of filling in the $STACK() variable. The mini-transaction created by
 * trigger_source_read_andor_verify() of course uses the critical section but that use, if the process is also
 * holding a lock perhaps in use by another process holding the lock we need, creates a deadlock. So we avoid
 * doing anything with triggers in the event of an error in this fashion.
 */
int trigger_locate_andor_load(mstr *trigname, rhdtyp **rtn_vec)
{
	mstr_len_t		origlen;
	char			*ptr, *ptr_beg, *ptr_top;
	boolean_t		runtime_disambiguator_specified;
	gd_region		*reg;
	mstr			regname, gbl;
	mident			rtn_name;
	gd_region		*save_gv_cur_region;
	gv_key			save_currkey[DBKEYALLOC(MAX_KEY_SZ)];
	gv_namehead		*gvt;
	gv_namehead		*save_gv_target;
	gvnh_reg_t		*gvnh_reg;
	gv_trigger_t		*trigdsc;
	gvt_trigger_t		*gvt_trigger;
	rtn_tabent		*rttabent;
	sgm_info		*save_sgm_info_ptr;
	jnlpool_addrs_ptr_t	save_jnlpool;
	rhdtyp			*rtn_vector;
	sgmnt_addrs		*csa, *regcsa;
	sgmnt_data_ptr_t	csd;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	assert(NULL != trigname);
	assert((NULL != trigname->addr) && (0 != trigname->len));
	if (NULL == gd_header)
		gvinit();
	DBGTRIGR((stderr, "trigger_locate_andor_load: Entered with $tlevel=%d, $trigdepth=%d\n",
		  dollar_tlevel, gtm_trigger_depth));
	/*
	 * Input parameter "trigname" is of the form
	 *	a) <21-BYTE-MAX-TRUNCATED-GBLNAME>#<AUTO-GENERATED-CNT>#[RUNTIME-DISAMBIGUATOR][/REGION-NAME] OR
	 *	b) <28-BYTE-USER-SPECIFIED-TRIGNAME>#[RUNTIME-DISAMBIGUATOR][/REGION-NAME]
	 * where
	 *	<21-BYTE-MAX-TRUNCATED-GBLNAME>#<AUTO-GENERATED-CNT> OR <28-BYTE-USER-SPECIFIED-TRIGNAME> is the
	 *		auto-generated or user-specified trigger name we are searching for
	 *	RUNTIME-DISAMBIGUATOR is the unique string appended at the end by the runtime to distinguish
	 *		multiple triggers in different regions with the same auto-generated or user-given name
	 *	REGION-NAME is the name of the region in the gld where we specifically want to search for trigger names
	 *	[] implies optional parts
	 *
	 * Example usages are
	 *	  x#         : trigger routine user-named "x"
	 *	  x#1#       : trigger routine auto-named "x#1"
	 *	  x#1#A      : trigger routine auto-named "x#1" but also runtime disambiguated by "#A" at the end
	 *	  x#/BREG    : trigger routine user-named "x" in region BREG
	 *	  x#A/BREG   : trigger routine user-named "x", runtime disambiguated by "#A", AND in region BREG
	 *	  x#1#/BREG  : trigger routine auto-named "x#1" in region BREG
	 *	  x#1#A/BREG : trigger routine auto-named "x#1", runtime disambiguated by "#A", AND in region BREG
	 */
	/* First lets locate the trigger. Try simple way first - lookup in routine name table.
	 * But "find_rtn_tabent" function has no clue about REGION-NAME so remove /REGION-NAME (if any) before invoking it.
	 */
	regname.len = 0;
	reg = NULL;
	origlen = trigname->len;	/* Save length in case need to restore it later */
	for (ptr_beg = trigname->addr, ptr_top = ptr_beg + trigname->len, ptr = ptr_top - 1; ptr >= ptr_beg; ptr--)
	{
		/* If we see a '#' and have not yet seen a '/' we are sure no region-name disambiguator has been specified */
		if ('#' == *ptr)
			break;
		if ('/' == *ptr)
		{
			trigname->len = ptr - trigname->addr;
			ptr++;
			regname.addr = ptr;
			regname.len = ptr_top - ptr;
			reg = find_region(&regname);	/* find region "regname" in "gd_header" */
			if (NULL == reg)
			{	/* Specified region-name is not present in current gbldir.
	 			 * Treat non-existent region name as if trigger was not found.
				 */
				ISSUE_TRIGNAMENF_ERROR_IF_APPROPRIATE(trigname);
				return TRIG_FAILURE_RC;
			}
			break;
		}
	}
	if (NULL != *rtn_vec)
		rtn_vector = *rtn_vec;
	else if (find_rtn_tabent(&rttabent, trigname))
		rtn_vector = rttabent->rt_adr;
	else
		rtn_vector = NULL;
	DBGTRIGR((stderr, "trigger_locate_andor_load: routine was %sfound (1)\n", (NULL == rtn_vector)?"not ":""));
	/* If we have the trigger routine header, do some validation on it, else keep looking */
	SAVE_REGION_INFO(save_currkey, save_gv_target, save_gv_cur_region, save_sgm_info_ptr, save_jnlpool);
	runtime_disambiguator_specified = ('#' != trigname->addr[trigname->len - 1]);
	if (!runtime_disambiguator_specified && (NULL != reg))
	{	/* Region-name has been specified and no runtime-disambiguator specified. Need to further refine the
		 * search done by find_rtn_tabent to focus on the desired region in case multiple routines with the same
		 * trigger name (but different runtime-disambiguators) exist.
		 */
		rtn_name.len = MIN(trigname->len, MAX_MIDENT_LEN);
		rtn_name.addr = trigname->addr;
		if (!reg->open)
			gv_init_reg(reg, NULL);	/* Open the region before obtaining "csa" */
		regcsa = &FILE_INFO(reg)->s_addrs;
		assert('#' == rtn_name.addr[rtn_name.len - 1]);
		for ( ; rttabent <= rtn_names_end; rttabent++)
		{
			if ((rttabent->rt_name.len < rtn_name.len) || memcmp(rttabent->rt_name.addr, rtn_name.addr, rtn_name.len))
			{	/* Past the list of routines with same name as trigger but different runtime disambiguators */
				rtn_vector = NULL;
				break;
			}
			rtn_vector = rttabent->rt_adr;
			trigdsc = (gv_trigger_t *)rtn_vector->trigr_handle;
			gvt_trigger = trigdsc->gvt_trigger;
			gvt = gvt_trigger->gv_target;
			/* Target region and trigger routine's region do not match, continue */
			if (gvt->gd_csa != regcsa)
				continue;
			/* Check if global name associated with the trigger is indeed mapped to the corresponding region
			 * by the gld.  If not treat this case as if the trigger is invisible and move on
			 */
			gbl.addr = gvt->gvname.var_name.addr;
			gbl.len = gvt->gvname.var_name.len;
			TP_CHANGE_REG_IF_NEEDED(gvt->gd_csa->region);
			csa = cs_addrs;
			csd = csa->hdr;
			COMPUTE_HASH_MNAME(&gvt->gvname);
			GV_BIND_NAME_ONLY(gd_header, &gvt->gvname, gvnh_reg);	/* does tp_set_sgm() */
			if (((NULL == gvnh_reg->gvspan) && (gv_cur_region != reg))
			    || ((NULL != gvnh_reg->gvspan) && !gvnh_spanreg_ismapped(gvnh_reg, gd_header, reg)))
				continue;
			/* Target region and trigger routine's region match, break (this check is a formality) */
			if (gvt->gd_csa == regcsa)
				break;
		}
	}
	csa = NULL;
	if (NULL == rtn_vector)
	{	/* If runtime disambiguator was specified and routine is not found, look no further.
		 * Otherwise, look for it in the #t global of any (or specified) region in current gbldir.
		 */
		if (0 < origlen)
			trigname->len = origlen;	/* Restore length to include region disambiguator */
		DBGTRIGR((stderr, "trigger_locate_andor_load: find trigger by name without disambiguator\n"));
		if (runtime_disambiguator_specified
			|| (TRIG_FAILURE_RC == trigger_source_read_andor_verify(trigname, &rtn_vector)))
		{
			RESTORE_REGION_INFO(save_currkey, save_gv_target, save_gv_cur_region, save_sgm_info_ptr, save_jnlpool);
			ISSUE_TRIGNAMENF_ERROR_IF_APPROPRIATE(trigname);
			return TRIG_FAILURE_RC;
		}
		trigdsc = (gv_trigger_t *)rtn_vector->trigr_handle;
		assert(NULL != rtn_vector);
	} else
	{	/* Have a routine header addr. From that we can get the gv_trigger_t descriptor and from that, the
		 * gvt_trigger and other necessities.
		 */
		DBGTRIGR((stderr, "trigger_locate_andor_load: routine header found\n"));
		trigdsc = (gv_trigger_t *)rtn_vector->trigr_handle;
		gvt_trigger = trigdsc->gvt_trigger;			/* We now know our base block now */
		gvt = gv_target = gvt_trigger->gv_target;		/* gv_target contains global name */
		gbl.addr = gvt->gvname.var_name.addr;
		gbl.len = gvt->gvname.var_name.len;
		TP_CHANGE_REG_IF_NEEDED(gvt->gd_csa->region);
		csa = cs_addrs;
		csd = csa->hdr;
		if (runtime_disambiguator_specified && (NULL != reg))
		{	/* Runtime-disambiguator has been specified and routine was found. But region-name-disambiguator
			 * has also been specified. Check if found routine is indeed in the specified region. If not
			 * treat it as a failure to find the trigger.
			 */
			if (!reg->open)
				gv_init_reg(reg, NULL);
			if (&FILE_INFO(reg)->s_addrs != csa)
			{
				RESTORE_REGION_INFO(save_currkey, save_gv_target, save_gv_cur_region, save_sgm_info_ptr,
							save_jnlpool);
				ISSUE_TRIGNAMENF_ERROR_IF_APPROPRIATE(trigname);
				return TRIG_FAILURE_RC;
			}
			/* Check if global name is indeed mapped to this region by the gld.  If not treat this case as
			 * if the trigger is invisible and issue an error
			 */
			COMPUTE_HASH_MNAME(&gvt->gvname);
			GV_BIND_NAME_ONLY(gd_header, &gvt->gvname, gvnh_reg);	/* does tp_set_sgm() */
			if (((NULL == gvnh_reg->gvspan) && (gv_cur_region != reg))
			    || ((NULL != gvnh_reg->gvspan) && !gvnh_spanreg_ismapped(gvnh_reg, gd_header, reg)))
			{
				RESTORE_REGION_INFO(save_currkey, save_gv_target, save_gv_cur_region, save_sgm_info_ptr,
							save_jnlpool);
				ISSUE_TRIGNAMENF_ERROR_IF_APPROPRIATE(trigname);
				return TRIG_FAILURE_RC;
			}
		}
		assert(csd == cs_data);
	}
	DBGTRIGR((stderr, "trigger_locate_andor_load: leaving with source from rtnhdr 0x%lx\n",
		  (*rtn_vec) ? (*((rhdtyp **)rtn_vec))->trigr_handle : NULL));
	RESTORE_REGION_INFO(save_currkey, save_gv_target, save_gv_cur_region, save_sgm_info_ptr, save_jnlpool);
	assert(NULL != rtn_vector);
	assert(trigdsc == rtn_vector->trigr_handle);
	*rtn_vec = rtn_vector;
	return 0;
}

#endif /* GTM_TRIGGER */
