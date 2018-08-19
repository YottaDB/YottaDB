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

#include "mdef.h"

#include "gtm_string.h"
#include "stp_parms.h"
#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsblk.h"
#include "gdsfhead.h"
#include "error.h"
#include "cli.h"
#include "iosp.h"
#include "gdscc.h"
#include "gdskill.h"
#include "filestruct.h"
#include "jnl.h"
#include "buddy_list.h"		/* needed for tp.h */
#include "hashtab_int4.h"	/* needed for tp.h */
#include "tp.h"
#include "mu_int_maps.h"
#include "util.h"
#include "mupipbckup.h"
#include "dbfilop.h"
#include "targ_alloc.h"
#include "mupip_exit.h"
#ifdef UNIX
#include "ipcrmid.h"
#include "db_ipcs_reset.h"
#endif
#include "mu_getlst.h"
#include "mu_outofband_setup.h"
#include "mupip_integ.h"
#include "gtmmsg.h"
#include "collseq.h"
#include "db_snapshot.h"
#include "mupint.h"
#include "mu_gv_cur_reg_init.h"
#include "warn_db_sz.h"

#define DUMMY_GLOBAL_VARIABLE		"%D%DUMMY_VARIABLE"
#define DUMMY_GLOBAL_VARIABLE_LEN	SIZEOF(DUMMY_GLOBAL_VARIABLE)
#define MAX_UTIL_LEN			96
#define APPROX_ALL_ERRORS		1000000
#define DEFAULT_ERR_LIMIT		10
#define PERCENT_FACTOR			100
#define PERCENT_DECIMAL_SCALE		100000
#define PERCENT_SCALE_FACTOR		1000

#define TEXT1 " is incorrect, should be "
#define TEXT2 "!/Largest transaction number found in database was "
#define TEXT3 "Current transaction number is                    "
#define MSG1  "!/!/WARNING: Transaction number reset complete on all active blocks. Please do a DATABASE BACKUP before proceeding"
/* The QWPERCENTCALC calculates the percent used in two scaled parts*/

#define QWPERCENTCALC(lpt, rpt, sizes, int_blks, blk_size) 									\
{																\
	if (int_blks)														\
	{															\
		lpt = ((sizes) * PERCENT_FACTOR) / ((int_blks) * (blk_size));							\
		rpt = ((((sizes) * PERCENT_DECIMAL_SCALE) / ((int_blks) * (blk_size))) - (lpt * PERCENT_SCALE_FACTOR));		\
	} else															\
		lpt = rpt = 0;													\
}

#define CUMULATE_TOTAL(T_TYPE, IDX)												\
{																\
	for (c_type = BLKS; c_type < CUM_TYPE_MAX; c_type++)									\
	{															\
		GTM_WHITE_BOX_TEST(WBTEST_FAKE_BIG_CNTS, mu_int_cum[c_type][IDX], (mu_int_cum[c_type][IDX] << 31));		\
		mu_int_tot[T_TYPE][c_type] += mu_int_cum[c_type][IDX];								\
		mu_int_cum[c_type][IDX] = 0;											\
	}															\
}

GBLDEF unsigned char		mu_int_root_level;
GBLDEF uint4			mu_int_adj[MAX_BT_DEPTH + 1];
GBLDEF uint4			mu_int_errknt;
GBLDEF uint4			mu_int_offset[MAX_BT_DEPTH + 1];
GBLDEF uint4			mu_int_skipreg_cnt = 0;
GBLDEF gtm_uint64_t		mu_int_cum[CUM_TYPE_MAX][MAX_BT_DEPTH + 1];
GBLDEF int			disp_map_errors;
GBLDEF int			disp_maxkey_errors;
GBLDEF int			disp_trans_errors;
GBLDEF int			maxkey_errors = 0;
GBLDEF int			muint_adj;
GBLDEF int			mu_int_plen;
GBLDEF int			mu_map_errs;
GBLDEF int			trans_errors = 0;
GBLDEF boolean_t		block = FALSE;
GBLDEF boolean_t		muint_fast = FALSE;
GBLDEF boolean_t		master_dir;
GBLDEF boolean_t		muint_key = MUINTKEY_FALSE;
GBLDEF boolean_t		muint_subsc = FALSE;
GBLDEF boolean_t		mu_int_err_ranges;
GBLDEF boolean_t		tn_reset_specified;	/* use this to avoid recomputing cli_present("TN_RESET") in the loop */
GBLDEF boolean_t		tn_reset_this_reg;
GBLDEF boolean_t		mu_region_found;
GBLDEF block_id			mu_int_adj_prev[MAX_BT_DEPTH + 1];
GBLDEF block_id			mu_int_path[MAX_BT_DEPTH + 1];
GBLDEF global_list		*trees_tail;
GBLDEF global_list		*trees;
GBLDEF sgmnt_data		mu_int_data;
GBLDEF unsigned char		*mu_int_master;
GBLDEF trans_num		largest_tn;
GBLDEF int4			mu_int_blks_to_upgrd;
GBLDEF span_node_integ		*sndata;
GBLDEF boolean_t		null_coll_type_err = FALSE;
GBLDEF boolean_t		null_coll_type;
GBLDEF unsigned int		rec_num;
GBLDEF block_id			blk_id;
GBLDEF boolean_t		nct_err_type;
GBLDEF int			rec_len;
/* The following global variable is used to store the encryption information for the current database. The variable is initialized
 * in mu_int_init (mupip integ -file <file.dat>) and mu_int_reg (mupip integ -reg <reg_name>).
 */
GBLDEF enc_handles		mu_int_encr_handles;
GBLDEF boolean_t		ointeg_this_reg;
GBLDEF util_snapshot_ptr_t	util_ss_ptr;
GBLDEF boolean_t		preserve_snapshot;
GBLDEF boolean_t		online_specified;

GBLREF bool			mu_ctrly_occurred;
GBLREF bool			mu_ctrlc_occurred;
GBLREF bool			error_mupip;
GBLREF short			crash_count;
GBLREF gd_region		*gv_cur_region;
GBLREF gv_namehead		*gv_target;
GBLREF gv_key			*gv_altkey;
GBLREF gv_key			*gv_currkey;
GBLREF sgmnt_addrs		*cs_addrs;
GBLREF tp_region		*grlist;
GBLREF bool			region;
GBLREF boolean_t		debug_mupip;
GBLREF gv_key			*muint_end_key;
GBLREF gv_key			*muint_start_key;

error_def(ERR_CTRLC);
error_def(ERR_CTRLY);
error_def(ERR_DBBTUFIXED);
error_def(ERR_DBBTUWRNG);
error_def(ERR_DBNOREGION);
error_def(ERR_DBRBNLBMN);
error_def(ERR_DBRBNNEG);
error_def(ERR_DBRBNTOOLRG);
error_def(ERR_DBRDONLY);
error_def(ERR_DBSPANCHUNKORD);
error_def(ERR_DBSPANGLOINCMP);
error_def(ERR_DBTNLTCTN);
error_def(ERR_DBTNRESET);
error_def(ERR_DBTNRESETINC);
error_def(ERR_INTEGERRS);
error_def(ERR_MUNOACTION);
error_def(ERR_MUNOFINISH);
error_def(ERR_MUNOTALLINTEG);
error_def(ERR_MUPCLIERR);
error_def(ERR_REGFILENOTFOUND);

void mupip_integ(void)
{
	boolean_t		full, muint_all_index_blocks, retvalue_mu_int_reg, region_was_frozen;
	boolean_t		update_filehdr, update_header_tn;
	boolean_t		online_integ = FALSE, stats_specified;
	char			*temp, util_buff[MAX_UTIL_LEN];
	unsigned char		dummy;
	unsigned char		key_buff[2048];
	short			iosb[4];
	unsigned short		keylen;
	int			idx, total_errors, util_len;
	uint4			cli_status, leftpt, mu_data_adj, mu_index_adj, prev_errknt, rightpt;
	block_id		dir_root, muint_block;
	enum cum_type		c_type;
	enum tot_type		t_type;
	file_control		*fc;
	gtm_uint64_t		blocks_free = (gtm_uint64_t)MAXUINT8;
	gtm_uint64_t		mu_int_tot[TOT_TYPE_MAX][CUM_TYPE_MAX], tot_blks, tot_recs;
	tp_region		*rptr;
	sgmnt_addrs		*csa;
	sgmnt_data_ptr_t	csd;
	span_node_integ		span_node_data;
	char			ss_filename[YDB_PATH_MAX];
	unsigned short		ss_file_len = YDB_PATH_MAX;
	unix_db_info		*udi;
	gd_region		*baseDBreg, *reg;
	sgmnt_addrs		*baseDBcsa;
	node_local_ptr_t	baseDBnl;
	sgmnt_addrs 		*tcsa;
	char 			*db_file_name;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	sndata = &span_node_data;
	error_mupip = FALSE;
	mu_region_found = TRUE;
	if (NULL == gv_target)
		gv_target = (gv_namehead *)targ_alloc(DUMMY_GLOBAL_VARIABLE_LEN, NULL, NULL);
	if (CLI_PRESENT == (cli_status = cli_present("MAXKEYSIZE")))
	{
		assert(SIZEOF(disp_maxkey_errors) == SIZEOF(int4));
		if (0 == cli_get_int("MAXKEYSIZE", (int4 *)&disp_maxkey_errors))
			mupip_exit(ERR_MUPCLIERR);
		if (disp_maxkey_errors < 1)
			disp_maxkey_errors = 1;
	} else  if (CLI_NEGATED == cli_status)
		disp_maxkey_errors = APPROX_ALL_ERRORS;
	else
		disp_maxkey_errors = DEFAULT_ERR_LIMIT;
	if (CLI_PRESENT == (cli_status = cli_present("TRANSACTION")))
	{
		assert(SIZEOF(disp_trans_errors) == SIZEOF(int4));
		if (0 == cli_get_int("TRANSACTION", (int4 *)&disp_trans_errors))
			mupip_exit(ERR_MUPCLIERR);
		if (disp_trans_errors < 1)
			disp_trans_errors = 1;
	} else  if (CLI_NEGATED == cli_status)
		disp_trans_errors = APPROX_ALL_ERRORS;
	else
		disp_trans_errors = DEFAULT_ERR_LIMIT;
	if (CLI_PRESENT == (cli_status = cli_present("MAP")))
	{
		assert(SIZEOF(disp_map_errors) == SIZEOF(int4));
		if (0 == cli_get_int("MAP", (int4 *)&disp_map_errors))
			mupip_exit(ERR_MUPCLIERR);
		if (disp_map_errors < 1)
			disp_map_errors = 1;
	} else  if (CLI_NEGATED == cli_status)
		disp_map_errors = APPROX_ALL_ERRORS;
	else
		disp_map_errors = DEFAULT_ERR_LIMIT;
	if (CLI_PRESENT == cli_present("ADJACENCY"))
	{
		assert(SIZEOF(muint_adj) == SIZEOF(int4));
		if (0 == cli_get_int("ADJACENCY", (int4 *)&muint_adj))
			mupip_exit(ERR_MUPCLIERR);
	} else
		muint_adj = DEFAULT_ADJACENCY;
	if (CLI_PRESENT == cli_present("BRIEF"))
		full = FALSE;
	else  if (CLI_PRESENT == cli_present("FULL"))
		full = TRUE;
	else
		full = FALSE;
	if (CLI_PRESENT == cli_present("FAST"))
		muint_fast = TRUE;
	else
		muint_fast = FALSE;
	/* DBG qualifier prints extra debug messages while waiting for KIP in region freeze */
	debug_mupip = (CLI_PRESENT == cli_present("DBG"));
	online_specified = (CLI_PRESENT == cli_present("ONLINE"));
	if (online_specified)
	{ 	/* if MUPIP INTEG -ONLINE -ANALYZE=<filename> is given then display details about the snapshot file
		 * and do early return
		 */
		if (cli_get_str("ANALYZE", ss_filename, &ss_file_len))
		{
			ss_anal_shdw_file(ss_filename, ss_file_len);
			return;
		}
	}
	stats_specified = FALSE;
	if ((CLI_PRESENT == cli_present("REGION")) || online_specified)
	{
		/* MUPIP INTEG -REG -STATS should work on statsdbs. So enable statsdb region visibility in gld */
		assert(FALSE == TREF(ok_to_see_statsdb_regs));
		if (CLI_PRESENT == cli_present("STATS"))
		{
			TREF(ok_to_see_statsdb_regs) = TRUE;
			TREF(statshare_opted_in) = NO_STATS_OPTIN;	/* Do not open statsdb automatically when basedb is opened.
									 * This is needed in the "mu_int_reg" calls done below.
									 */
			stats_specified = TRUE;
		}
		gvinit(); /* side effect: initializes gv_altkey (used by code below) & gv_currkey (not used by below code) */
		if (stats_specified)
			TREF(ok_to_see_statsdb_regs) = FALSE;
		region = TRUE;
		mu_getlst("WHAT", SIZEOF(tp_region));
		if (!grlist)
		{
			error_mupip = TRUE;
			gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_DBNOREGION);
			mupip_exit(ERR_MUNOACTION);
		}
		if (!mu_region_found)
			error_mupip = FALSE;
		rptr = grlist;
	} else
		GVKEY_INIT(gv_altkey, DBKEYSIZE(MAX_KEY_SZ));	/* used by code below */
	online_integ = ((TRUE != cli_negated("ONLINE")) && region); /* Default option for INTEG is -ONLINE */
	preserve_snapshot = (CLI_PRESENT == cli_present("PRESERVE")); /* Should snapshot file be preserved ? */
	assert(!online_integ || (region && !tn_reset_specified));
	assert(MUINTKEY_FALSE == muint_key);
	if (CLI_PRESENT == cli_present("SUBSCRIPT"))
	{
		keylen = SIZEOF(key_buff);
		if (0 == cli_get_str("SUBSCRIPT", (char *)key_buff, &keylen))
			mupip_exit(ERR_MUPCLIERR);
		if (FALSE == mu_int_getkey(key_buff, keylen))
			mupip_exit(ERR_MUPCLIERR);
		assert(muint_key);	/* or else "mu_int_getkey" call above would have returned FALSE */
		disp_map_errors = 0;
	}
	if (CLI_PRESENT == cli_present("BLOCK"))
	{
		if (0 == cli_get_hex("BLOCK", (uint4 *)&muint_block))
			mupip_exit(SS_NORMAL);
		block = TRUE;
		disp_map_errors = 0;
		master_dir = FALSE;
	}
	muint_all_index_blocks = !(block || muint_key);
	mu_int_master = malloc(MASTER_MAP_SIZE_MAX);
	tn_reset_specified = (CLI_PRESENT == cli_present("TN_RESET"));
	mu_outofband_setup();
	UNIX_ONLY(ESTABLISH(mu_int_ch);)
	if (region)
	{
		if (online_integ)
		{
			/* The below structure members will be assigned in ss_initiate done in mu_int_reg.
			 * No free required as will be gone when process dies
			 */
			util_ss_ptr = malloc(SIZEOF(util_snapshot_t));
			util_ss_ptr->header = &mu_int_data;
			util_ss_ptr->master_map = mu_int_master;
			util_ss_ptr->native_size = 0;
		} else /* Establish the condition handler ONLY if ONLINE INTEG was not requested */
			ESTABLISH(mu_freeze_ch);
	}
	for (total_errors = mu_int_errknt = 0;  ;  total_errors += mu_int_errknt, mu_int_errknt = 0)
	{
		if (mu_ctrly_occurred || mu_ctrlc_occurred)
			break;
		if (region)
		{
			assert(NULL != rptr);
			reg = rptr->reg;
			if (!mupfndfil(reg, NULL, LOG_ERROR_TRUE))
			{
				mu_int_skipreg_cnt++;
				rptr = rptr->fPtr;
				if (NULL == rptr)
					break;
				continue;
			}
		}
		memset(mu_int_tot, 0, SIZEOF(mu_int_tot));
		memset(mu_int_cum, 0, SIZEOF(mu_int_tot));
		mu_index_adj = mu_data_adj = 0;
		mu_int_err_ranges = (CLI_NEGATED != cli_present("KEYRANGES"));
		mu_int_root_level = BML_LEVL;	/* start with what is an invalid level for a root block */
		mu_map_errs = 0, prev_errknt = 0, largest_tn = 0;
		mu_int_blks_to_upgrd = 0;
		mu_int_path[0] = 0;
		mu_int_offset[0] = 0;
		mu_int_plen = 1;
		memset(mu_int_adj, 0, SIZEOF(mu_int_adj));
		sndata->sn_cnt = 0;
		sndata->sn_blk_cnt = 0;
		sndata->sn_type = SN_NOT;
		if (region)
		{
			if (stats_specified)
			{	/* -STATS has been specified. So only work on the stats region, not the base region.
				 * "mu_getlst" would have added only the base region in the list. Replace that with
				 * the corresponding stats region and continue integ. But first open base region db
				 * in order to determine stats region. It is safe to do this replacement as mupip integ
				 * does not get crit across all regions. If it did, then statsdb region cannot replace
				 * the basedb region as ftok order across all involved regions matter in that case (and
				 * only "insert_region" knows to insert based on that ordering but that is not possible
				 * for statsdb region because the statsdb file location is not known at "mu_getlst" time).
				 */
				assert(NO_STATS_OPTIN == TREF(statshare_opted_in));	/* So we only open basedb & not statsdb */
				mu_int_reg(reg, &retvalue_mu_int_reg, RETURN_AFTER_DB_OPEN_TRUE);	/* sets "gv_cur_region" */
				/* Copy statsdb file name into statsdb region and then do "mupfndfil" to check if the file exists */
				if (retvalue_mu_int_reg)
				{
					baseDBreg = reg;
					baseDBcsa = &FILE_INFO(baseDBreg)->s_addrs;
					baseDBnl = baseDBcsa->nl;
					BASEDBREG_TO_STATSDBREG(baseDBreg, reg);
					COPY_STATSDB_FNAME_INTO_STATSREG(reg, baseDBnl->statsdb_fname, baseDBnl->statsdb_fname_len);
					if (!mupfndfil(reg, NULL, LOG_ERROR_FALSE))
					{	/* statsDB does not exist. Print an info message and skip to next region */
						gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_REGFILENOTFOUND, 4,
											DB_LEN_STR(reg), REG_LEN_STR(reg));
						retvalue_mu_int_reg = FALSE;
					}
				}
			} else
				retvalue_mu_int_reg = TRUE;
			if (retvalue_mu_int_reg)
			{
				util_out_print("!/!/Integ of region !AD", TRUE, REG_LEN_STR(reg));
				ointeg_this_reg = online_integ;	/* used by "mu_int_reg" if called with RETURN_AFTER_DB_OPEN_FALSE */
				mu_int_reg(reg, &retvalue_mu_int_reg, RETURN_AFTER_DB_OPEN_FALSE); /* sets "gv_cur_region" */
			}
			if (!retvalue_mu_int_reg)
			{
				rptr = rptr->fPtr;
				if (NULL == rptr)
					break;
				continue;
			}
			csa = cs_addrs;
			/* If the region was frozen (INTEG -REG -NOONLINE) then use cs_addrs->hdr for verification of
			 * blks_to_upgrd, free blocks calculation. Otherwise (ONLINE INTEG) then use mu_int_data for
			 * the verification.
			 */
			region_was_frozen = !ointeg_this_reg;
			if (region_was_frozen)
				csd = csa->hdr;
			else
				csd = &mu_int_data;
		} else
		{
			region_was_frozen = FALSE; /* For INTEG -FILE, region is not frozen as we would have standalone access */
			if (FALSE == mu_int_init())	/* sets "gv_cur_region" */
			{
				if (mu_region_found)
					mupip_exit(ERR_INTEGERRS);
				else
					mupip_exit(ERR_MUNOACTION); /* Database file not found, no zero exit status */
			}
			csa = NULL;
			/* Since we have standalone access, there is no need for cs_addrs->hdr. So, use mu_int_data for
			 * verifications
			 */
			csd = &mu_int_data;
		}
		trees_tail = trees = (global_list *)malloc(SIZEOF(global_list));
		memset(trees, 0, SIZEOF(global_list));
		trees->root = dir_root = get_dir_root();
		master_dir = TRUE;
		trees_tail->nct = 0;
		trees_tail->act = 0;
		trees_tail->ver = 0;
		tn_reset_this_reg = update_header_tn = FALSE;
		if (tn_reset_specified)
		{
			if (gv_cur_region->read_only || (USES_NEW_KEY(csd)))
			{
				if (gv_cur_region->read_only)
					gtm_putmsg_csa(CSA_ARG(csa) VARLSTCNT(4) ERR_DBRDONLY, 2, DB_LEN_STR(gv_cur_region));
				else
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(4) MAKE_MSG_TYPE(ERR_TEXT, ERROR), 2,
						LEN_AND_LIT("Database is being (re)encrypted"));
				mu_int_errknt++;
				mu_int_err(ERR_DBTNRESET, 0, 0, 0, 0, 0, 0, 0);
				mu_int_errknt -= 2;
				/* is this error supposed to update error count, or leave it ( then mu_int_errknt-- instead)*/
				mu_int_plen++;  /* continuing, so compensate for mu_int_err decrement */
			} else
				tn_reset_this_reg = update_header_tn = TRUE;
		}
		if (block)
		{
			master_dir = FALSE;
			trees->root = muint_block;
		}
		if ((MUINTKEY_NULLSUBS == muint_key) && gv_cur_region->std_null_coll)
		{	/* -SUBSCRIPT was specified AND at least one null-subscript was specified in it.
			 * muint_start_key and muint_end_key have been constructed assuming gv_cur_region->std_null_coll is FALSE.
			 * Update muint_start_key and muint_end_key to reflect the current gv_cur_region->std_null_coll value.
			 */
			GTM2STDNULLCOLL(muint_start_key->base, muint_start_key->end);
			GTM2STDNULLCOLL(muint_end_key->base, muint_end_key->end);
		}
		for (trees->link = 0;  ;  master_dir = FALSE, temp = (char*)trees,  trees = trees->link,  free(temp))
		{
			if (mu_ctrly_occurred || mu_ctrlc_occurred)
			{
				if (region_was_frozen)
				{
					region_freeze(reg, FALSE, FALSE, FALSE, FALSE, FALSE);
					if (!reg->read_only)
					{
						fc = gv_cur_region->dyn.addr->file_cntl;
						fc->op = FC_WRITE;
						/* Note: cs_addrs->hdr points to shared memory and is already aligned
						 * appropriately even if db was opened using O_DIRECT.
						 */
						fc->op_buff = (unsigned char *)FILE_INFO(reg)->s_addrs.hdr;
						fc->op_len = SGMNT_HDR_LEN;
						fc->op_pos = 1;
						dbfilop(fc);
					}
				}
				gtm_putmsg_csa(CSA_ARG(csa) VARLSTCNT(1) mu_ctrly_occurred ? ERR_CTRLY : ERR_CTRLC);
				mupip_exit(ERR_MUNOFINISH);
			}
			if (!trees)
				break;
			mu_int_path[0] = trees->root;
			if (trees->root < 0)
			{
				mu_int_err(ERR_DBRBNNEG, 0, 0, 0, 0, 0, 0, mu_int_root_level);
				continue;
			}
			if (trees->root >= mu_int_data.trans_hist.total_blks)
			{
				mu_int_err(ERR_DBRBNTOOLRG, 0, 0, 0, 0, 0, 0,
					mu_int_root_level);
				continue;
			}
			if (0 == (trees->root % mu_int_data.bplmap))
			{
				mu_int_err(ERR_DBRBNLBMN, 0, 0, 0, 0, 0, 0, mu_int_root_level);
				continue;
			}
			mu_int_plen = 0;
			memset(mu_int_adj_prev, 0, SIZEOF(mu_int_adj_prev));
			gv_target->nct = trees->nct;
			gv_target->act = trees->act;
			gv_target->ver = trees->ver;
			gv_altkey->prev = 0;
			assert(trees->keysize == strlen(trees->key));
			gv_altkey->end = trees->keysize;
			assert(gv_altkey->end + 2 <= gv_altkey->top);
			memcpy(gv_altkey->base, trees->key, gv_altkey->end);
			gv_altkey->base[gv_altkey->end++] = '\0';
			gv_altkey->base[gv_altkey->end] = '\0';
			if (gv_target->act)
				act_in_gvt(gv_target);
			if (mu_int_blk(trees->root, MAX_BT_DEPTH, TRUE, gv_altkey->base, gv_altkey->end, &dummy, 0, 0))
			{
				/* We are done with the INTEG CHECK for the current GVT, but if the spanning node INTEG
				 * check is not finished, either of the following two are occurred.
				 */
				if (SPAN_NODE == sndata->sn_type)
				{ /* ERROR 1: There is discontinuity in the spanning node blocks;
				   * adjacent spanning block is missing.
				   */
					mu_int_plen = mu_int_root_level + 1;
					mu_int_err(ERR_DBSPANGLOINCMP, TRUE, FALSE, sndata->span_node_buf, sndata->key_len,
							&dummy, 0, 0);
					sndata->sn_blk_cnt += sndata->span_blk_cnt;
					mu_int_plen = 0;
					sndata->sn_type = SN_NOT;
				}
				if (2 == sndata->sn_type)
				{ /* ERROR 2: Spanning-node-block occurred in the middle of non-spanning block */
					mu_int_plen = mu_int_root_level + 1;
					mu_int_err(ERR_DBSPANCHUNKORD, TRUE, FALSE, sndata->span_node_buf, sndata->key_len,
							&dummy, 0, 0);
					sndata->sn_blk_cnt += sndata->span_blk_cnt;
					mu_int_plen = 0;
					mu_int_plen = 0;
					sndata->sn_type = SN_NOT;
				}
				if (full)
				{
					if (trees->root == dir_root)
						util_out_print("!/Directory tree", TRUE);
					else
						util_out_print("!/Global variable ^!AD", TRUE, LEN_AND_STR(trees->key));
					if (mu_int_errknt > prev_errknt)
					{
						if (trees->root == dir_root)
							util_out_print("Total error count for directory tree:   !UL",
									TRUE, mu_int_errknt - prev_errknt);
						else
							util_out_print("Total error count for global !AD:	!UL.",
								TRUE, LEN_AND_STR(trees->key), mu_int_errknt - prev_errknt);
						prev_errknt = mu_int_errknt;
					}
					util_out_print("Level          Blocks         Records          % Used      Adjacent", TRUE);
					for (idx = mu_int_root_level;  idx >= 0;  idx--)
					{
						if ((0 == idx) && muint_fast && (trees->root != dir_root))
						util_out_print("!5UL !15@UQ              NA              NA  !12UL", TRUE,
								idx, &mu_int_cum[BLKS][idx],  mu_int_adj[idx]);
						else
						{
							QWPERCENTCALC(leftpt, rightpt, mu_int_cum[SIZE][idx],
								mu_int_cum[BLKS][idx], mu_int_data.blk_size);
							if (trees->root != dir_root)
							{
								util_out_print("!5UL !15@UQ !15@UQ    !8UL.!3ZL  !12UL",
									TRUE, idx, &mu_int_cum[BLKS][idx],
									&mu_int_cum[RECS][idx], leftpt, rightpt, mu_int_adj[idx]);
							} else
								util_out_print("!5UL !15@UQ !15@UQ    !8UL.!3ZL           NA",
									TRUE, idx, &mu_int_cum[BLKS][idx],
									&mu_int_cum[RECS][idx], leftpt, rightpt);
						}
					}
				}
				if (dir_root == trees->root)
				{
					for (idx = mu_int_root_level;  idx >= 0;  idx--)
						CUMULATE_TOTAL(DIRTREE, idx);
					mu_int_adj[0] = 0;
				} else
				{
					for (idx = mu_int_root_level;  idx > 0;  idx--)
					{
						CUMULATE_TOTAL(INDX, idx);
						mu_index_adj += mu_int_adj[idx];
						mu_int_adj[idx] = 0;
					}
					CUMULATE_TOTAL(DATA, 0);
					mu_data_adj += mu_int_adj[0];
					mu_int_adj[0] = 0;
				}
			} else  if (update_header_tn)
			{
				update_header_tn = FALSE;
				mu_int_err(ERR_DBTNRESETINC, 0, 0, 0, 0, 0, 0, 0);
				mu_int_plen++;  /* continuing, so compensate for mu_int_err decrement */
				mu_int_errknt--; /* if this error is not supposed to increment the error count */
			}
		}
		if ((MUINTKEY_NULLSUBS == muint_key) && gv_cur_region->std_null_coll)
		{	/* muint_start_key and muint_end_key have been modified for this region. Undo that change. */
			STD2GTMNULLCOLL(muint_start_key->base, muint_start_key->end);
			STD2GTMNULLCOLL(muint_end_key->base, muint_end_key->end);
		}
		if (muint_all_index_blocks)
		{
			mu_int_maps();
			if (!mu_int_errknt)
			{	/* because it messes with the totals, the white box case does not produce an accurate result */
				blocks_free = (gtm_uint64_t)mu_int_data.trans_hist.total_blks
					- (((gtm_uint64_t)mu_int_data.trans_hist.total_blks + (gtm_uint64_t)mu_int_data.bplmap - 1)
					/ (gtm_uint64_t)mu_int_data.bplmap)
					- mu_int_tot[DATA][BLKS] - mu_int_tot[INDX][BLKS] - mu_int_tot[DIRTREE][BLKS];
				/* If ONLINE INTEG, then cs_addrs->hdr->trans_hist.free_blocks can no longer be expected to remain
				 * the same as it was during the time INTEG started as updates are allowed when ONLINE INTEG is
				 * in progress and hence use mu_int_data.trans_hist.free_blocks as it is the copy of the file header
				 * right when ONLINE INTEG starts.
				 */
				if (csd->trans_hist.free_blocks != blocks_free)
				{
					if (gv_cur_region->read_only)
						mu_int_errknt++;
					util_len = SIZEOF("!/Free blocks counter in file header:  ") - 1;
					memcpy(util_buff, "!/Free blocks counter in file header:  ", util_len);
					util_len += i2hexl_nofill(csd->trans_hist.free_blocks,
							(uchar_ptr_t)&util_buff[util_len], 16);
					MEMCPY_LIT(&util_buff[util_len], TEXT1);
					util_len += SIZEOF(TEXT1) - 1;
					util_len += i2hexl_nofill(blocks_free, (uchar_ptr_t)&util_buff[util_len], 16);
					util_buff[util_len] = 0;
					util_out_print(util_buff, TRUE);
				} else
					blocks_free = (gtm_uint64_t)MAXUINT8;
			}
			if (!muint_fast && (mu_int_blks_to_upgrd != csd->blks_to_upgrd))
			{
				gtm_putmsg_csa(CSA_ARG(csa)
					VARLSTCNT(4) ERR_DBBTUWRNG, 2, mu_int_blks_to_upgrd, csd->blks_to_upgrd);
				if (gv_cur_region->read_only || mu_int_errknt)
					mu_int_errknt++;
				else
					gtm_putmsg_csa(CSA_ARG(csa) VARLSTCNT(1) ERR_DBBTUFIXED);
			}
			if (((0 != mu_int_data.kill_in_prog) || (0 != mu_int_data.abandoned_kills)) && (!mu_map_errs) && !region
				&& !gv_cur_region->read_only)
			{
				assert(mu_int_errknt > 0);
				mu_int_errknt--;
			}
		}
		if (muint_fast)
		{
			if (mu_int_errknt)
				util_out_print("!/Total error count from fast integ:		!UL.", TRUE, mu_int_errknt);
			else
				util_out_print("!/No errors detected by fast integ.", TRUE);
		} else
		{
			if (mu_int_errknt)
				util_out_print("!/Total error count from integ:		!UL.", TRUE, mu_int_errknt);
			else
				util_out_print("!/No errors detected by integ.", TRUE);
		}
		util_out_print("!/Type           Blocks         Records          % Used      Adjacent!/", TRUE);
		QWPERCENTCALC(leftpt, rightpt, mu_int_tot[DIRTREE][SIZE], mu_int_tot[DIRTREE][BLKS], mu_int_data.blk_size);
		util_out_print("Directory !11@UQ !15@UQ    !8UL.!3ZL            NA", TRUE, &mu_int_tot[DIRTREE][BLKS],
			&mu_int_tot[DIRTREE][RECS], leftpt, rightpt);
		QWPERCENTCALC(leftpt, rightpt, mu_int_tot[INDX][SIZE], mu_int_tot[INDX][BLKS], mu_int_data.blk_size);
		util_out_print("Index !15@UQ !15@UQ    !8UL.!3ZL  !12UL", TRUE, &mu_int_tot[INDX][BLKS],
			&mu_int_tot[INDX][RECS], leftpt, rightpt, mu_index_adj);
		if (muint_fast)
			util_out_print("Data  !15@UQ              NA              NA  !12UL", TRUE,
				&mu_int_tot[DATA][BLKS], mu_data_adj);
		else
		{
			QWPERCENTCALC(leftpt, rightpt, mu_int_tot[DATA][SIZE], mu_int_tot[DATA][BLKS], mu_int_data.blk_size);
			util_out_print("Data  !15@UQ !15@UQ    !8UL.!3ZL  !12UL", TRUE, &mu_int_tot[DATA][BLKS],
				&mu_int_tot[DATA][RECS], leftpt, rightpt, mu_data_adj);
		}
		if ((FALSE == block) && (MUINTKEY_FALSE == muint_key))
		{
			tot_blks = mu_int_data.trans_hist.total_blks
				- ((mu_int_data.trans_hist.total_blks + mu_int_data.bplmap - 1) / mu_int_data.bplmap);
			GTM_WHITE_BOX_TEST(WBTEST_FAKE_BIG_CNTS, tot_blks, tot_blks << 31);
			tot_blks = tot_blks - mu_int_tot[DATA][BLKS] - mu_int_tot[INDX][BLKS] - mu_int_tot[DIRTREE][BLKS];
			util_out_print("Free  !15@UQ              NA              NA            NA", TRUE, &tot_blks);
			tot_blks = mu_int_data.trans_hist.total_blks
					- (mu_int_data.trans_hist.total_blks + mu_int_data.bplmap - 1) / mu_int_data.bplmap;
			GTM_WHITE_BOX_TEST(WBTEST_FAKE_BIG_CNTS, tot_blks,
					mu_int_tot[DATA][BLKS] + mu_int_tot[INDX][BLKS] + mu_int_tot[DIRTREE][BLKS]);
		} else
			tot_blks = mu_int_tot[DATA][BLKS] + mu_int_tot[INDX][BLKS] + mu_int_tot[DIRTREE][BLKS];
		if (muint_fast)
			util_out_print("Total !15@UQ              NA              NA  !12UL", TRUE,
				&tot_blks, mu_data_adj + mu_index_adj);
		else
		{
			tot_recs = mu_int_tot[DIRTREE][RECS] + mu_int_tot[INDX][RECS] + mu_int_tot[DATA][RECS];
			util_out_print("Total !15@UQ !15@UQ              NA  !12UL", TRUE,
				&tot_blks, &tot_recs, mu_data_adj + mu_index_adj);
		}
		if (sndata->sn_cnt)
		{
			util_out_print("[Spanning Nodes:!UL ; Blocks:!UL]", TRUE, sndata->sn_cnt, sndata->sn_blk_cnt);
			/*[span_node:<no of span-node in DB>; blks: <total number of spanning blocks used by all span-node>]*/
		}
		if (largest_tn)
		{
			mu_int_err(ERR_DBTNLTCTN, 0, 0, 0, 0, 0, 0, 0);
			mu_int_plen--;
			mu_int_errknt--;
			if (trans_errors > disp_trans_errors)
			{
				util_out_print("Maximum number of transaction number errors to display:  !UL, was exceeded",
						TRUE, disp_trans_errors);
				util_out_print("!UL transaction number errors encountered.", TRUE, trans_errors);
			}
			MEMCPY_LIT(util_buff, TEXT2);
			util_len = SIZEOF(TEXT2) - 1;
			util_len += i2hexl_nofill(largest_tn, (uchar_ptr_t)&util_buff[util_len], 16);
			util_buff[util_len] = 0;
			util_out_print(util_buff, TRUE);
			MEMCPY_LIT(util_buff, TEXT3);
			util_len = SIZEOF(TEXT3) - 1;
			util_len += i2hexl_nofill(mu_int_data.trans_hist.curr_tn, (uchar_ptr_t)&util_buff[util_len], 16);
			util_buff[util_len] = 0;
			util_out_print(util_buff, TRUE);
		}
		if (maxkey_errors > disp_maxkey_errors)
		{
			util_out_print("Maximum number of keys too large errors to display:  !UL, was exceeded",
					TRUE, disp_maxkey_errors);
			util_out_print("!UL keys too large errors encountered.", TRUE, maxkey_errors);
		}
		if (region)
		{
			/* Below logic updates the database file header in the shared memory with values calculated
			 * by INTEG during it's course of tree traversal and writes it to disk. If ONLINE INTEG is
			 * in progress, then these values could legally be out-of-date and hence avoid writing the header if
			 * ONLINE INTEG is in progress.
			 */
			if (!gv_cur_region->read_only && !ointeg_this_reg)
			{
				if ((gtm_uint64_t)MAXUINT8 != blocks_free)
					csd->trans_hist.free_blocks = blocks_free;
				if (!mu_int_errknt && muint_all_index_blocks && !muint_fast)
				{
					if (mu_int_blks_to_upgrd != csd->blks_to_upgrd)
						csd->blks_to_upgrd = mu_int_blks_to_upgrd;
				}
				csd->span_node_absent = (sndata->sn_cnt) ? FALSE : TRUE;
				csd->maxkeysz_assured = (maxkey_errors) ? FALSE : TRUE;
				region_freeze(gv_cur_region, FALSE, FALSE, FALSE, FALSE, FALSE);
				fc = gv_cur_region->dyn.addr->file_cntl;
				fc->op = FC_WRITE;
				/* Note: cs_addrs->hdr points to shared memory and is already aligned
				 * appropriately even if db was opened using O_DIRECT.
				 */
				fc->op_buff = (unsigned char *)FILE_INFO(gv_cur_region)->s_addrs.hdr;
				fc->op_len = SGMNT_HDR_LEN;
				fc->op_pos = 1;
				dbfilop(fc);
			} else if (region_was_frozen)
			{	/* If online_integ, then database is not frozen, so no need to unfreeze. */
				region_freeze(gv_cur_region, FALSE, FALSE, FALSE, FALSE, FALSE);
			} else
			{
				assert(SNAPSHOTS_IN_PROG(csa));
				assert(NULL != csa->ss_ctx);
				ss_release(&csa->ss_ctx);
				CLEAR_SNAPSHOTS_IN_PROG(csa);
			}
			rptr = rptr->fPtr;
			if (NULL == rptr)
				break;
		} else if (!gv_cur_region->read_only)
		{
			assert(!online_integ);
			update_filehdr = FALSE;
			if (muint_all_index_blocks)
			{
				if ((0 == mu_map_errs) && ((0 != mu_int_data.kill_in_prog) || (0 != mu_int_data.abandoned_kills)))
				{
					mu_int_data.abandoned_kills = 0;
					mu_int_data.kill_in_prog = 0;
					update_filehdr = TRUE;
				}
				if ((MAXUINT8 != blocks_free) && (mu_int_data.trans_hist.free_blocks != blocks_free))
				{
					mu_int_data.trans_hist.free_blocks = blocks_free;
					update_filehdr = TRUE;
				}
				if (!mu_int_errknt && !muint_fast)
				{
					if (mu_int_blks_to_upgrd != mu_int_data.blks_to_upgrd)
					{
						mu_int_data.blks_to_upgrd = mu_int_blks_to_upgrd;
						update_filehdr = TRUE;
					}
				}
			}
			if (update_header_tn)
			{
				mu_int_data.trans_hist.early_tn = 2;
				mu_int_data.trans_hist.curr_tn = 1;
				/* curr_tn = 1 + 1 is done (instead of = 2) so as to use INCREMENT_CURR_TN macro.
				 * this way all places that update db curr_tn are easily obtained by searching for the macro.
				 * Reason for setting the transaction number to 2 (instead of 1) is so as to let a BACKUP in
				 * all forms to proceed, which earlier used to error out on seeing the database transaction
				 * number as 1.
				 */
				INCREMENT_CURR_TN(&mu_int_data);
				/* Reset all last backup transaction numbers to 1. */
				mu_int_data.last_inc_backup = 1;
				mu_int_data.last_com_backup = 1;
				mu_int_data.last_rec_backup = 1;
				mu_int_data.last_inc_bkup_last_blk = 0;
				mu_int_data.last_com_bkup_last_blk = 0;
				mu_int_data.last_rec_bkup_last_blk = 0;
				update_filehdr = TRUE;
				util_out_print(MSG1, TRUE);
			}
			if (update_filehdr)
			{
				fc = gv_cur_region->dyn.addr->file_cntl;
				fc->op = FC_WRITE;
				udi = FC2UDI(fc);
				if (udi->fd_opened_with_o_direct)
				{	/* Do aligned writes if opened with O_DIRECT */
					memcpy((TREF(dio_buff)).aligned, &mu_int_data, SGMNT_HDR_LEN);
					fc->op_buff = (sm_uc_ptr_t)(TREF(dio_buff)).aligned;
				} else
					fc->op_buff = (unsigned char *)&mu_int_data;
				fc->op_len = SGMNT_HDR_LEN;
				fc->op_pos = 1;
				dbfilop(fc);
				fc->op = FC_CLOSE;
				dbfilop(fc);
			}
			break;
		} else
			break;
	}
	if (!region)
	{
		db_ipcs_reset(gv_cur_region);
		mu_gv_cur_reg_free(); /* mu_gv_cur_reg_init done in mu_int_init() */
		REVERT;
	}
	total_errors += mu_int_errknt;
	if (error_mupip)
		total_errors++;
	if (mu_ctrly_occurred || mu_ctrlc_occurred)
	{
		gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(1) mu_ctrly_occurred ? ERR_CTRLY : ERR_CTRLC);
		mupip_exit(ERR_MUNOFINISH);
	}
	if (0 != total_errors)
		mupip_exit(ERR_INTEGERRS);
	if (0 != mu_int_skipreg_cnt)
		mupip_exit(ERR_MUNOTALLINTEG);
	if (!mu_region_found)
		mupip_exit(ERR_MUNOACTION);
	tcsa = REG2CSA(gv_cur_region);
	if ((NULL != tcsa) && (NULL != tcsa->ti) && (0 != tcsa->ti->total_blks) && (NULL != tcsa->hdr)
		&& (0 != MAXTOTALBLKS(tcsa->hdr)))
	{
  		db_file_name = (char *)gv_cur_region->dyn.addr->fname;
		warn_db_sz(db_file_name, 0, tcsa->ti->total_blks, MAXTOTALBLKS(tcsa->hdr));
	}
	mupip_exit(SS_NORMAL);
}
