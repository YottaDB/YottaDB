/****************************************************************
 *								*
 * Copyright (c) 2010-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef TRIGGER_UPDATE_PROTOS_H_INCLUDED
#define TRIGGER_UPDATE_PROTOS_H_INCLUDED

STATICFNDEF boolean_t trigger_already_exists(char *trigvn, int trigvn_len, char **values, uint4 *value_len,	/* input parm */
						stringkey *set_trigger_hash, stringkey *kill_trigger_hash,	/* input parm */
						int *set_index, int *kill_index, boolean_t *set_cmp_result,	/* output parm */
						boolean_t *kill_cmp_result, boolean_t *full_match,		/* output parm */
						mval *setname, mval *killname);					/* output parm */
STATICFNDCL int4 modify_record(char *trigvn, int trigvn_len, char add_delete, int trigger_index, char **values, uint4 *value_len,
			       mval *trigger_count, boolean_t db_matched_set, boolean_t db_matched_kill, stringkey *kill_hash,
			       stringkey *set_hash, int set_kill_bitmask);
STATICFNDCL int4 gen_trigname_sequence(char *trigvn, uint4 trigvn_len, mval *trigger_count, char *trigname_seq_str, uint4 seq_len);
STATICFNDCL int4 add_trigger_hash_entry(char *trigvn, int trigvn_len, char *cmd_value, int trigindx, boolean_t add_kill_hash,
					stringkey *kill_hash, stringkey *set_hash);
STATICFNDCL int4 add_trigger_cmd_attributes(char *trigvn, int trigvn_len,  int trigger_index, char *trig_cmds, char **values,
						uint4 *value_len, boolean_t db_matched_set, boolean_t db_matched_kill,
						stringkey *kill_hash, stringkey *set_hash, uint4 db_cmd_bm, uint4 tf_cmd_bm);
STATICFNDCL int4 add_trigger_options_attributes(char *trigvn, int trigvn_len, int trigger_index, char *trig_options, char **values,
						uint4 *value_len);
STATICFNDCL boolean_t subtract_trigger_cmd_attributes(char *trigvn, int trigvn_len, char *trig_cmds, char **values,
						      uint4 *value_len, boolean_t set_cmp, stringkey *kill_hash,
						      stringkey *set_hash, int trigger_index, uint4 db_cmd_bm, uint4 tf_cmd_bm);
STATICFNDCL boolean_t validate_label(char *trigvn, int trigvn_len);
STATICFNDCL int4 update_commands(char *trigvn, int trigvn_len, int trigger_index, char *new_trig_cmds, char *orig_db_cmds);
STATICFNDCL int4 update_trigger_name(char *trigvn, int trigvn_len, int trigger_index, char *db_trig_name, char *tf_trig_name,
				     uint4 tf_trig_name_len);
STATICFNDCL boolean_t trigger_update_rec_helper(mval *trigger_rec, boolean_t noprompt, uint4 *trig_stats);

boolean_t trigger_name_search(char *trigger_name, uint4 trigger_name_len, mval *val, gd_region **found_reg);
boolean_t check_unique_trigger_name_full(char **values, uint4 *value_len, mval *val, boolean_t *new_match,
				char *trigvn, int trigvn_len, stringkey *kill_trigger_hash, stringkey *set_trigger_hash);
boolean_t trigger_update_rec(mval *trigger_rec, boolean_t noprompt, uint4 *trig_stats, io_pair *trigfile_device,
			     int4 *record_num);

STATICFNDCL trig_stats_t trigupdrec_reg(char *trigvn, uint4 trigvn_len, boolean_t *jnl_format_done, mval *trigjrec,
	boolean_t *new_name_check_done, boolean_t *new_name_ptr, char **values, uint4 *value_len, char add_delete,
	stringkey *kill_trigger_hash, stringkey *set_trigger_hash, char *disp_trigvn, int disp_trigvn_len, uint4 *trig_stats,
	boolean_t *first_gtmio, char *utilprefix, int *utilprefixlen);

boolean_t trigger_update(mval *trigger_rec);
#endif
