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

 /* General repository for global variable definitions. This keeps us from
  * pulling in modules and all their references when all we wanted was the
  * global data def.
  *
  * Note, all GBLDEF fields are automatically cleared to zeroes. No initialization to
  * zero (0) or NULL or any other value that translates to zeroes is necessary on these
  * fields and should be avoided as it creates extra linker and image startup processing
  * that can only slow things down even if only by a little.
  */

#include "mdef.h"

#include "gtm_inet.h"
#include "gtm_iconv.h"
#include "gtm_socket.h"
#include "gtm_unistd.h"
#include "gtm_limits.h"
#include "gtm_un.h"
#include "gtm_pwd.h"
#include "gtm_signal.h"

#include <sys/time.h>
#include "cache.h"
#include "gtm_multi_thread.h"
#include "hashtab_addr.h"
#include "hashtab_int4.h"
#include "hashtab_int8.h"
#include "hashtab_mname.h"
#include "hashtab_str.h"
#include "hashtab_objcode.h"
/* The define of CHEXPAND below causes error.h to create GBLDEFs */
#define CHEXPAND
#include "error.h"
#include <rtnhdr.h>
#include "gdsroot.h"
#include "gdskill.h"
#include "ccp.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "gdscc.h"
#include "comline.h"
#include "compiler.h"
#include "cmd_qlf.h"
#include "io.h"
#include "iosp.h"
#include "jnl.h"
#include "lv_val.h"
#include "mdq.h"
#include "mprof.h"
#include "mv_stent.h"
#include "stack_frame.h"
#include "stp_parms.h"
#include "stringpool.h"
#include "buddy_list.h"		/* needed for tp.h */
#include "tp.h"
#include "tp_frame.h"
#include "mlkdef.h"
#include "zshow.h"
#include "zwrite.h"
#include "zbreak.h"
#include "mmseg.h"
#include "gtmsiginfo.h"
#include "gtmimagename.h"
#include "gt_timer.h"
#include "iosocketdef.h"	/* needed for socket_pool and MAX_N_SOCKETS */
#include "ctrlc_handler_dummy.h"
#include "unw_prof_frame_dummy.h"
#include "op.h"
#include "gtmsecshr.h"
#include "error_trap.h"
#include "patcode.h"	/* for pat_everything and sizeof_pat_everything */
#include "source_file.h"	/* for REV_TIME_BUFF_LEN */
#include "mupipbckup.h"
#include "dpgbldir.h"
#include "mmemory.h"
#include "have_crit.h"
#include "alias.h"
/* FOR REPLICATION RELATED GLOBALS */
#include "repl_msg.h"
#include "gtmsource.h"
#include "gtmrecv.h"
/* FOR MERGE RELATED GLOBALS */
#include "gvname_info.h"
#include "op_merge.h"
#include "cli.h"
#include "invocation_mode.h"
#include "fgncal.h"
#include "parse_file.h"		/* for MAX_FBUFF */
#include "repl_sem.h"
#include "gtm_zlib.h"
#include "anticipatory_freeze.h"
#include "mu_rndwn_all.h"
#include "jnl_typedef.h"
#include "repl_ctl.h"
#include "gds_blk_upgrade.h"	/* for UPGRADE_IF_NEEDED flag */
#include "cws_insert.h"		/* for CWS_REORG_ARRAYSIZE */
#include "gtm_multi_proc.h"
#include "fnpc.h"
#ifdef UNICODE_SUPPORTED
#include "gtm_icu_api.h"
#include "gtm_utf8.h"
#include "gtm_conv.h"
#include "utfcgr.h"
#endif
#include "gtmcrypt.h"
#include "gdsblk.h"
#include "muextr.h"
#include "gtmxc_types.h"
#ifdef GTM_TLS
#include "gtm_tls_interface.h"
#endif
#ifdef GTM_TRIGGER
#include "gv_trigger.h"
#include "gtm_trigger.h"
#endif
#ifdef DEBUG
#include "wcs_wt.h"
#endif
#define DEFAULT_ZERROR_STR	"Unprocessed $ZERROR, see $ZSTATUS"
#define DEFAULT_ZERROR_LEN	(SIZEOF(DEFAULT_ZERROR_STR) - 1)
#include "gtm_libaio.h"
#include "gtcm.h"

GBLDEF	gd_region		*db_init_region;
GBLDEF	sgmnt_data_ptr_t	cs_data;
GBLDEF	sgmnt_addrs		*cs_addrs;
GBLDEF	sgmnt_addrs		*cs_addrs_list;	/* linked list of csa corresponding to all currently open databases */

GBLDEF	unsigned short	proc_act_type;
GBLDEF	volatile bool	ctrlc_pending;
GBLDEF	bool		undef_inhibit;
GBLDEF	volatile int4	ctrap_action_is;
GBLDEF	bool		out_of_time;
GBLDEF	io_pair		io_curr_device;		/* current device	*/
GBLDEF	io_pair		io_std_device;		/* standard device	*/
GBLDEF	io_log_name	*dollar_principal;	/* pointer to log name GTM$PRINCIPAL if defined */
GBLDEF	bool		prin_in_dev_failure;
GBLDEF	bool		prin_out_dev_failure;
GBLDEF	io_desc		*active_device;
GBLDEF	bool		error_mupip,
			file_backed_up,
			gv_replopen_error,
			gv_replication_error,
			incremental,
			jobpid,
			online,
			record,
			std_dev_outbnd,
			in_mupip_freeze,
	                in_backup,
	                view_debug1,
	                view_debug2,
	                view_debug3,
	                view_debug4,
			mupip_error_occurred,
	                dec_nofac;
GBLDEF	boolean_t	is_updproc,
			mupip_jnl_recover,
			suspend_lvgcol,
			run_time,
			unhandled_stale_timer_pop,
			gtcm_connection,
			is_replicator,		/* TRUE => this process can write jnl records to the jnlpool for replicated db */
	                tp_in_use,		/* TRUE => TP has been used by this process and is thus initialized */
			dollar_truth = TRUE,
			ydb_stdxkill,		/* TRUE => Use M Standard X-KILL - FALSE use historical GTM X-KILL (default) */
			in_timed_tn,		/* TRUE => Timed TP transaction in progress */
			tp_timeout_deferred;	/* TRUE => A TP timeout has occurred but is deferred */
GBLDEF	uint4		is_updhelper;		/* = UPD_HELPER_READER if reader helper, = UPD_HELPER_WRITER if writer helper,
						 * = 0 otherwise.
						 */
GBLDEF	volatile boolean_t tp_timeout_set_xfer;	/* TRUE => A timeout succeeded in setting xfer table intercepts. This flag stays
						 * a true global unless each thread gets its own xfer table.
						 */
GBLDEF	VSIG_ATOMIC_T	forced_exit;		/* Asynchronous signal/interrupt handler sets this variable to TRUE,
						 * hence the VSIG_ATOMIC_T type in the definition.
						 */
GBLDEF	intrpt_state_t	intrpt_ok_state = INTRPT_OK_TO_INTERRUPT;	/* any other value implies it is not ok to interrupt */
GBLDEF	unsigned char	*msp,
			*restart_ctxt,
			*stackbase,
			*stacktop,
			*stackwarn,
			*restart_pc;
GBLDEF	int4		backup_close_errno,
			backup_write_errno,
			mubmaxblk,
			forced_exit_err,
			exit_state,
			restore_read_errno;
GBLDEF	volatile int4	outofband;
GBLDEF	int		mumps_status = SS_NORMAL;
GBLDEF	gtm_uint64_t	stp_array_size;
GBLDEF	gvzwrite_datablk	*gvzwrite_block;
GBLDEF	lvzwrite_datablk	*lvzwrite_block;
GBLDEF	io_log_name	*io_root_log_name;
GBLDEF	mliteral	literal_chain;
GBLDEF	mstr		*comline_base,
			*err_act,
			**stp_array,
			extnam_str,
			env_gtm_env_xlate;
GBLDEF MSTR_CONST(default_sysid, "gtm_sysid");	/* Keep this as "gtm_sysid" and not "ydb_sysid" for backward compatibility
						 * with GT.M applications that migrate to YottaDB.
						 */
GBLDEF	mval		dollar_zgbldir,
			dollar_zsource = DEFINE_MVAL_STRING(MV_STR, 0, 0, 0, NULL, 0, 0),
			dollar_zstatus,
			ztrap_pop2level = DEFINE_MVAL_STRING(MV_NM | MV_INT, 0, 0, 0, 0, 0, 0),
			zstep_action,
			dollar_system,
			dollar_estack_delta = DEFINE_MVAL_STRING(MV_STR, 0, 0, 0, NULL, 0, 0),
			dollar_zerror = DEFINE_MVAL_STRING(MV_STR, 0, 0, DEFAULT_ZERROR_LEN, DEFAULT_ZERROR_STR, 0, 0),
			dollar_zyerror,
			dollar_ztexit = DEFINE_MVAL_STRING(MV_STR, 0, 0, 0, NULL, 0, 0);
GBLDEF  uint4		dollar_zjob;
GBLDEF	mval		dollar_zinterrupt;
GBLDEF	boolean_t	dollar_zininterrupt;
GBLDEF	boolean_t	dollar_ztexit_bool; /* Truth value of dollar_ztexit when coerced to boolean */
GBLDEF	boolean_t	dollar_zquit_anyway;
GBLDEF	mv_stent	*mv_chain;
GBLDEF	sgm_info	*first_sgm_info;	/* List of participating regions in the TP transaction with NO ftok ordering */
GBLDEF	sgm_info	*first_tp_si_by_ftok;	/* List of participating regions in the TP transaction sorted on ftok order */
GBLDEF	spdesc		indr_stringpool,
			rts_stringpool,
			stringpool;
GBLDEF	stack_frame	*frame_pointer;
GBLDEF	stack_frame	*zyerr_frame;
GBLDEF	symval		*curr_symval;
GBLDEF	tp_frame	*tp_pointer;
GBLDEF	tp_region	*halt_ptr,
			*grlist;
GBLDEF	trans_num	local_tn;		/* transaction number for THIS PROCESS (starts at 0 each time) */
GBLDEF	trans_num	tstart_local_tn;	/* copy of global variable "local_tn" at op_tstart time */
GBLDEF	gv_namehead	*gv_target;
GBLDEF	gv_namehead	*gv_target_list;	/* List of ALL gvts that were allocated (in targ_alloc) by this process */
GBLDEF	gv_namehead	*gvt_tp_list;		/* List of gvts that were referenced in the current TP transaction */
GBLDEF	gvt_container	*gvt_pending_list;	/* list of gvts that need to be re-examined/re-allocated when region is opened */
GBLDEF	buddy_list	*gvt_pending_buddy_list;/* buddy_list for maintaining memory for gv_targets to be re-examined/allocated */
GBLDEF	buddy_list	*noisolation_buddy_list;	/* a buddy_list for maintaining the globals that are noisolated */
GBLDEF	int4		exi_condition;
GBLDEF	uint4		ydbDebugLevel;
GBLDEF	boolean_t	ydbSystemMalloc;
GBLDEF	caddr_t		smCallerId;			/* Caller of top level malloc/free */
GBLDEF	int		process_exiting;
GBLDEF	int4		dollar_zsystem;
GBLDEF	int4		dollar_zeditor;
GBLDEF	rtn_tabent	*rtn_fst_table, *rtn_names, *rtn_names_top, *rtn_names_end;
GBLDEF	int4		break_message_mask;
GBLDEF	bool		rc_locked;
GBLDEF	boolean_t	certify_all_blocks;		/* If flag is set all blocks are checked after they are
							 * written to the database.  Upon error we stay critical
							 * and report.  This flag can be set via the MUMPS command
							 * VIEW "GDSCERT":1. */
GBLDEF	gd_addr		*original_header;
GBLDEF	hash_table_str	*complits_hashtab;
GBLDEF	hash_table_str	*compsyms_hashtab;
GBLDEF	mem_list	*mem_list_head;
GBLDEF	boolean_t	debug_mupip;
GBLDEF	unsigned char	t_fail_hist[CDB_MAX_TRIES];	/* type has to be unsigned char and not enum cdb_sc to ensure single byte */
GBLDEF	cache_rec_ptr_t	cr_array[((MAX_BT_DEPTH * 2) - 1) * 2];	/* Maximum number of blocks that can be in transaction */
GBLDEF	unsigned int	cr_array_index;
GBLDEF	boolean_t	need_core;		/* Core file should be created */
GBLDEF	boolean_t	created_core;		/* core file was created */
GBLDEF	unsigned int	core_in_progress;	/* creating core NOW if > 0 */
GBLDEF	boolean_t	dont_want_core;		/* Higher level flag overrides need_core set by lower level rtns */
GBLDEF	boolean_t	exit_handler_active;	/* recursion prevention */
GBLDEF	boolean_t	skip_exit_handler;	/* set for processes that are usually forked off and so should not do gds_rundown */
GBLDEF	boolean_t	block_saved;
GBLDEF	gtm_chset_t	dse_over_chset = CHSET_M;
LITDEF	MIDENT_DEF(zero_ident, 0, NULL);		/* the null mident */
GBLDEF	int4		aligned_source_buffer[MAX_SRCLINE / SIZEOF(int4) + 1];
GBLDEF	src_line_struct	src_head;
GBLDEF	short int	source_line;
GBLDEF	int		source_column;
GBLDEF	bool		devctlexp;
GBLDEF 	char		cg_phase;       /* code generation phase */
/* Previous code generation phase: Only used by emit_code.c to initialize the push list at the
 * beginning of each phase (bug fix: C9D12-002478) */
GBLDEF 	char		cg_phase_last;
GBLDEF	int		cmd_cnt;
GBLDEF	command_qualifier	glb_cmd_qlf = { CQ_DEFAULT },
				cmd_qlf = { CQ_DEFAULT };
#ifdef __osf__
#pragma pointer_size (save)
#pragma pointer_size (long)
#endif
GBLDEF	char		**cmd_arg;
#ifdef __osf__
#pragma pointer_size (restore)
#endif
GBLDEF	boolean_t	oldjnlclose_started;
/* DEFERRED EVENTS */
GBLDEF	bool		licensed = TRUE;

GBLDEF	volatile int4		num_deferred;
GBLDEF	volatile	int4	fast_lock_count;	/* Used in wcs_stale */
/* REPLICATION RELATED GLOBALS */
GBLDEF	gtmsource_options_t	gtmsource_options;
GBLDEF	gtmrecv_options_t	gtmrecv_options;

GBLDEF	boolean_t		is_tracing_on;
GBLDEF	void			(*tp_timeout_start_timer_ptr)(int4 tmout_sec) = tp_start_timer_dummy;
GBLDEF	void			(*tp_timeout_clear_ptr)(void) = tp_clear_timeout_dummy;
GBLDEF	void			(*tp_timeout_action_ptr)(void) = tp_timeout_action_dummy;
GBLDEF	void			(*ctrlc_handler_ptr)() = ctrlc_handler_dummy;
GBLDEF	int			(*op_open_ptr)(mval *v, mval *p, mval *t, mval *mspace) = op_open_dummy;
GBLDEF	void			(*unw_prof_frame_ptr)(void) = unw_prof_frame_dummy;
/* Initialized only in gtm_startup() */
GBLDEF	void			(*jnl_file_close_timer_ptr)(void);
GBLDEF	void			(*fake_enospc_ptr)(void);
GBLDEF	void			(*simple_timeout_timer_ptr)(TID tid, int4 hd_len, boolean_t **timedout);

#ifdef UNICODE_SUPPORTED
GBLDEF	u_casemap_t 		gtm_strToTitle_ptr;		/* Function pointer for gtm_strToTitle */
#endif
GBLDEF	boolean_t		mu_reorg_process;		/* set to TRUE by MUPIP REORG */
GBLDEF	boolean_t		mu_reorg_in_swap_blk;		/* set to TRUE for the duration of the call to "mu_swap_blk" */
GBLDEF	boolean_t		mu_rndwn_process;
GBLDEF	gv_key			*gv_currkey_next_reorg;
GBLDEF	gv_namehead		*reorg_gv_target;
GBLDEF	struct sockaddr_un	gtmsecshr_sock_name;
GBLDEF	struct sockaddr_un	gtmsecshr_cli_sock_name;
GBLDEF	key_t			gtmsecshr_key;
GBLDEF	int			gtmsecshr_sockpath_len;
GBLDEF	int			gtmsecshr_cli_sockpath_len;
GBLDEF	mstr			gtmsecshr_pathname;
GBLDEF	int			server_start_tries;
GBLDEF	int			gtmsecshr_sockfd = FD_INVALID;
GBLDEF	boolean_t		gtmsecshr_sock_init_done;
GBLDEF	char			muext_code[MUEXT_MAX_TYPES][2] =
{
#	define MUEXT_TABLE_ENTRY(muext_rectype, code0, code1)	{code0, code1},
#	include "muext_rec_table.h"
#	undef MUEXT_TABLE_ENTRY
};
GBLDEF	int			patch_is_fdmp;
GBLDEF	int			patch_fdmp_recs;
GBLDEF	boolean_t		horiz_growth;
GBLDEF	int4			prev_first_off, prev_next_off;
				/* these two globals store the values of first_off and next_off in cse,
				 * when there is a blk split at index level. This is to permit rollback
				 * to intermediate states */
GBLDEF	sm_uc_ptr_t		min_mmseg;
GBLDEF	sm_uc_ptr_t		max_mmseg;
GBLDEF	mmseg			*mmseg_head;
GBLDEF	ua_list			*first_ua, *curr_ua;
GBLDEF	char			*update_array, *update_array_ptr;
GBLDEF	int			gv_fillfactor = 100,
				rc_set_fragment;       		/* Contains offset within data at which data fragment starts */
GBLDEF	uint4			update_array_size,
				cumul_update_array_size;	/* the current total size of the update array */
GBLDEF	kill_set		*kill_set_tail;
GBLDEF	int			pool_init;
GBLDEF	boolean_t		is_src_server;
GBLDEF	boolean_t		is_rcvr_server;
GBLDEF	jnl_format_buffer	*non_tp_jfb_ptr;
GBLDEF	boolean_t		dse_running;
GBLDEF	jnlpool_addrs_ptr_t	jnlpool;
GBLDEF	jnlpool_addrs_ptr_t	jnlpool_head;
GBLDEF	recvpool_addrs		recvpool;
GBLDEF	int			recvpool_shmid = INVALID_SHMID;
GBLDEF	int			gtmsource_srv_count;
GBLDEF	int			gtmrecv_srv_count;
/* The following _in_prog counters are needed to prevent deadlocks while doing jnl-qio (timer & non-timer). */
GBLDEF	volatile int4		db_fsync_in_prog;
GBLDEF	volatile int4		jnl_qio_in_prog;
GBLDEF	gtmsiginfo_t		signal_info;
#ifndef MUTEX_MSEM_WAKE
GBLDEF	int			mutex_sock_fd = FD_INVALID;
GBLDEF	struct sockaddr_un	mutex_sock_address;
GBLDEF	struct sockaddr_un	mutex_wake_this_proc;
GBLDEF	int			mutex_wake_this_proc_len;
GBLDEF	int			mutex_wake_this_proc_prefix_len;
GBLDEF	fd_set			mutex_wait_on_descs;
#endif
GBLDEF	void			(*call_on_signal)();
GBLDEF	enum gtmImageTypes	image_type;	/* initialized at startup i.e. in dse.c, lke.c, gtm.c, mupip.c, gtmsecshr.c etc. */
GBLDEF	unsigned int		invocation_mode = MUMPS_COMPILE; /* how mumps has been invoked */
GBLDEF	char			cli_err_str[MAX_CLI_ERR_STR] = "";   /* Parse Error message buffer */
GBLDEF	char			*cli_err_str_ptr;
GBLDEF	boolean_t		gtm_pipe_child;
GBLDEF	io_desc			*gtm_err_dev;
/* this array is indexed by file descriptor */
/* Latch variable for Unix implementations. Used in SUN and HP */
GBLDEF	global_latch_t		defer_latch;
GBLDEF	int			num_additional_processors;
GBLDEF	int			gtm_errno = -1;		/* holds the errno (unix) in case of an rts_error */
GBLDEF	int4			error_condition;
GBLDEF	global_tlvl_info	*global_tlvl_info_head;
GBLDEF	buddy_list		*global_tlvl_info_list;
GBLDEF	boolean_t		job_try_again;
GBLDEF	volatile int4		gtmMallocDepth;		/* Recursion indicator */
GBLDEF	d_socket_struct		*socket_pool;
GBLDEF	boolean_t		mu_star_specified;
GBLDEF	backup_reg_list		*mu_repl_inst_reg_list;
GBLDEF	volatile int		is_suspended;		/* TRUE if this process is currently suspended */
GBLDEF	gv_namehead		*reset_gv_target = INVALID_GV_TARGET;
GBLDEF	VSIG_ATOMIC_T		util_interrupt;
GBLDEF	sgmnt_addrs		*kip_csa;
GBLDEF	boolean_t		need_kip_incr;
GBLDEF	int			merge_args;
GBLDEF	merge_glvn_ptr		mglvnp;
GBLDEF	boolean_t		ztrap_new;
GBLDEF	int4			wtfini_in_prog;
#ifdef DEBUG
/* Items for $piece stats */
GBLDEF	int	c_miss;				/* cache misses (debug) */
GBLDEF	int	c_hit;				/* cache hits (debug) */
GBLDEF	int	c_small;			/* scanned small string brute force */
GBLDEF	int	c_small_pcs;			/* chars scanned by small scan */
GBLDEF	int	c_pskip;			/* number of pieces "skipped" */
GBLDEF	int	c_pscan;			/* number of pieces "scanned" */
GBLDEF	int	c_parscan;			/* number of partial scans (partial cache hits) */
GBLDEF	int	cs_miss;			/* cache misses (debug) */
GBLDEF	int	cs_hit;				/* cache hits (debug) */
GBLDEF	int	cs_small;			/* scanned small string brute force */
GBLDEF	int	cs_small_pcs;			/* chars scanned by small scan */
GBLDEF	int	cs_pskip;			/* number of pieces "skipped" */
GBLDEF	int	cs_pscan;			/* number of pieces "scanned" */
GBLDEF	int	cs_parscan;			/* number of partial scans (partial cache hits) */
GBLDEF	int	c_clear;			/* cleared due to (possible) value change */
GBLDEF	boolean_t	setp_work;
#ifdef UNICODE_SUPPORTED
/* Items for UTF8 cache */
GBLDEF	int	u_miss;				/* UTF cache misses (debug) */
GBLDEF	int	u_hit;				/* UTF cache hits (debug) */
GBLDEF	int	u_small;			/* UTF scanned small string brute force (debug) */
GBLDEF	int	u_pskip;			/* Number of UTF groups "skipped" (debug) */
GBLDEF	int	u_puscan;			/* Number of groups "scanned" for located char (debug) */
GBLDEF	int	u_pabscan;			/* Number of non-UTF groups we scan for located char (debug) */
GBLDEF	int	u_parscan;			/* Number of partial scans (partial cache hits) (debug) */
GBLDEF	int	u_parhscan;			/* Number of partial scans after filled slots (debug) */
#endif /* UNICODE_SUPPORTED */
#endif /* DEBUG */
GBLDEF z_records	zbrk_recs;
GBLDEF	ipcs_mesg	db_ipcs;		/* For requesting gtmsecshr to update ipc fields */
GBLDEF	gd_region	*ftok_sem_reg;		/* Last region for which ftok semaphore is grabbed */
GBLDEF	int		ydb_non_blocked_write_retries; /* number of retries for non-blocked write to pipe */
GBLDEF	boolean_t		write_after_image;	/* true for after-image jnlrecord writing by recover/rollback */
GBLDEF	int			iott_write_error;
GBLDEF	int4			write_filter;
GBLDEF	boolean_t		need_no_standalone;
GBLDEF	int4	zdir_form = ZDIR_FORM_FULLPATH; /* $ZDIR shows full path including DEVICE and DIRECTORY */
GBLDEF	mval	dollar_zdir = DEFINE_MVAL_STRING(MV_STR, 0, 0, 0, NULL, 0, 0);
GBLDEF	int * volatile		var_on_cstack_ptr; /* volatile pointer to int; volatile so that nothing gets optimized out */
GBLDEF	hash_table_int4		cw_stagnate;
GBLDEF	boolean_t		cw_stagnate_reinitialized;

GBLDEF	uint4		pat_everything[] = { 0, 2, PATM_E, 1, 0, PAT_MAX_REPEAT, 0, PAT_MAX_REPEAT, 1 }; /* pattern = ".e" */
GBLDEF	mstr_len_t	sizeof_pat_everything = SIZEOF(pat_everything);
GBLDEF	uint4		*pattern_typemask;
GBLDEF	pattern		*pattern_list;
GBLDEF	pattern		*curr_pattern;
/* Unicode related data */
GBLDEF	boolean_t	gtm_utf8_mode;		/* Is GT.M running with Unicode Character Set; Set only after ICU initialization */
GBLDEF	boolean_t	is_ydb_chset_utf8;	/* Is ydb_chset environment variable set to UTF8 */
GBLDEF	boolean_t	utf8_patnumeric;	/* Should patcode N match non-ASCII numbers in pattern match ? */
GBLDEF	boolean_t	badchar_inhibit;	/* Suppress malformed UTF-8 characters by default */
GBLDEF  MSTR_DEF(dollar_zchset, 1, "M");
GBLDEF  MSTR_DEF(dollar_zpatnumeric, 1, "M");
GBLDEF  MSTR_DEF(dollar_zpin, 3, "< /");
GBLDEF  MSTR_DEF(dollar_zpout, 3, "> /");
GBLDEF  MSTR_DEF(dollar_prin_log, 1, "0");
/* Standard MUMPS pattern-match table.
 * This table holds the current pattern-matching attributes of each ASCII character.
 * Bits 0..23 of each entry correspond with the pattern-match characters, A..X.
 */
GBLDEF pattern mumps_pattern = {
	(void *) 0,		/* flink */
	(void *) 0,		/* typemask */
	(void *) 0,		/* pat YZ name array */
	(void *) 0,		/* pat YZ name-length array */
	-1,			/* number of YZ patcodes */
	1,			/* namlen */
	{'M', '\0'}		/* name */
};
/* mapbit is used by pattab.c and patstr.c. Note that patstr.c uses only entries until PATM_X */
GBLDEF	readonly uint4	mapbit[] =
{
	PATM_A, PATM_B, PATM_C, PATM_D, PATM_E, PATM_F, PATM_G, PATM_H,
	PATM_I, PATM_J, PATM_K, PATM_L, PATM_M, PATM_N, PATM_O, PATM_P,
	PATM_Q, PATM_R, PATM_S, PATM_T, PATM_U, PATM_V, PATM_W, PATM_X,
	PATM_YZ1, PATM_YZ2, PATM_YZ3, PATM_YZ4
};
LITDEF	uint4	typemask[PATENTS] =
{
	PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C,	/* hex 00-07 : ASCII characters */
	PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C,	/* hex 08-0F : ASCII characters */
	PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C,	/* hex 10-17 : ASCII characters */
	PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C,	/* hex 18-1F : ASCII characters */
	PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P,	/* hex 20-27 : ASCII characters */
	PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P,	/* hex 28-2F : ASCII characters */
	PATM_N, PATM_N, PATM_N, PATM_N, PATM_N, PATM_N, PATM_N, PATM_N,	/* hex 30-37 : ASCII characters */
	PATM_N, PATM_N, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P,	/* hex 38-3F : ASCII characters */
	PATM_P, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U,	/* hex 40-47 : ASCII characters */
	PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U,	/* hex 48-4F : ASCII characters */
	PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U,	/* hex 50-57 : ASCII characters */
	PATM_U, PATM_U, PATM_U, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P,	/* hex 58-5F : ASCII characters */
	PATM_P, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L,	/* hex 60-67 : ASCII characters */
	PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L,	/* hex 68-6F : ASCII characters */
	PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L,	/* hex 70-77 : ASCII characters */
	PATM_L, PATM_L, PATM_L, PATM_P, PATM_P, PATM_P, PATM_P, PATM_C,	/* hex 78-7F : ASCII characters */
	PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C,	/* hex 80-87 : non-ASCII characters */
	PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C,	/* hex 88-8F : non-ASCII characters */
	PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C,	/* hex 90-97 : non-ASCII characters */
	PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C, PATM_C,	/* hex 98-9F : non-ASCII characters */
	PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P,	/* hex A0-A7 : non-ASCII characters */
	PATM_P, PATM_P, PATM_L, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P,	/* hex A8-AF : non-ASCII characters */
	PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P,	/* hex B0-B7 : non-ASCII characters */
	PATM_P, PATM_P, PATM_L, PATM_P, PATM_P, PATM_P, PATM_P, PATM_P,	/* hex B8-BF : non-ASCII characters */
	PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U,	/* hex C0-C7 : non-ASCII characters */
	PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U,	/* hex C8-CF : non-ASCII characters */
	PATM_P, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U,	/* hex D0-D7 : non-ASCII characters */
	PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_U, PATM_P, PATM_L,	/* hex D8-DF : non-ASCII characters */
	PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L,	/* hex E0-E7 : non-ASCII characters */
	PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L,	/* hex E8-EF : non-ASCII characters */
	PATM_P, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L,	/* hex F0-F7 : non-ASCII characters */
	PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_L, PATM_P, PATM_C	/* hex F8-FF : non-ASCII characters */
};
GBLDEF	uint4		pat_allmaskbits;	/* universal set of valid pattern bit codes for currently active pattern table */
/* globals related to caching of pattern evaluation match result.
 * for a given <strptr, strlen, patptr, depth> tuple, we store the evaluation result.
 * in the above,
 *	<strptr, strlen>	uniquely identifies a substring of the input string.
 *	<patptr>		identifies the pattern atom that we are matching with
 *	<depth>			identifies the recursion depth of do_patalt() for this pattern atom ("repcnt" in code)
 * note that <depth> is a necessity in the above because the same alternation pattern atom can have different
 *	match or not-match status for the same input string depending on the repetition count usage of the pattern atom
 * after a series of thoughts on an efficient structure for storing pattern evaluation, finally arrived at a simple
 *	array of structures wherein for a given length (strlen) we have a fixed number of structures available.
 * we allocate an array of structures, say, 1024 structures.
 * this is a simple 1-1 mapping, wherein
 *	for length 0, the available structures are the first 32 structures of the array,
 *	for length 1, the available structures are the second 32 structures of the array.
 *	...
 *	for length 47, the available structures are the 47th 32 structures of the array.
 *	for length 48 and above, the available structures are all the remaining structures of the array.
 * whenever any new entry needs to be cached and there is no room among the available structures, we preempt the
 *	most unfrequently used cache entry (to do this we do keep a count of every entry's frequency of usage)
 * the assumption is that substrings of length > 48 (an arbitrary reasonable small number) won't be used
 *	so frequently so that they have lesser entries to fight for among themselves than lower values of length.
 * with the above caching in place, the program segment below took 15 seconds.
 * it was found that if the array size is increased to 16384 (as opposed to 1024 as above) and the available
 *	structures for each length increased proportionally (i.e. 16 times = 16*32 structures instead of 32 as above)
 *	the performance improved to the extent of taking 3 seconds.
 * but this raised an interesting question, that of "size" vs. "time" tradeoff.
 * with increasing array size, we get better "time" performance due to better caching.
 * but that has an overhead of increased "size" (memory) usage.
 * to arrive at a compromise, a dynamic algorithm emerged. the process will allocate a small array
 *	beginning at 1024 entries and grow to a max of 16384 entries as and when it deems the hit ratio is not good.
 * the array only grows, i.e. there is no downsizing algorithm at play.
 * the dynamic algorithm addresses to an extent both the "size" and "time" issues and finishes the below in 1 second.
 * #defines for the dynamic algorithm growth can be found in patcode.h
 */
GBLDEF	int4		curalt_depth = -1;				/* depth of alternation nesting */
GBLDEF	int4		do_patalt_calls[PTE_MAX_CURALT_DEPTH];		/* number of calls to do_patalt() */
GBLDEF	int4		do_patalt_hits[PTE_MAX_CURALT_DEPTH];		/* number of pte_csh hits in do_patalt() */
GBLDEF	int4		do_patalt_maxed_out[PTE_MAX_CURALT_DEPTH];	/* no. of pte_csh misses after maxing on allocation size */

GBLDEF	pte_csh		*pte_csh_array[PTE_MAX_CURALT_DEPTH];		/* pte_csh array (per curalt_depth) */
GBLDEF	int4		pte_csh_cur_size[PTE_MAX_CURALT_DEPTH];		/* current pte_csh size (per curalt_depth) */
GBLDEF	int4		pte_csh_alloc_size[PTE_MAX_CURALT_DEPTH];	/* current allocated pte_csh size (per curalt_depth) */
GBLDEF	int4		pte_csh_entries_per_len[PTE_MAX_CURALT_DEPTH];	/* current number of entries per len */
GBLDEF	int4		pte_csh_tail_count[PTE_MAX_CURALT_DEPTH];	/* count of non 1-1 corresponding pte_csh_array members */

GBLDEF	pte_csh		*cur_pte_csh_array;			/* copy of pte_csh_array corresponding to curalt_depth */
GBLDEF	int4		cur_pte_csh_size;			/* copy of pte_csh_cur_size corresponding to curalt_depth */
GBLDEF	int4		cur_pte_csh_entries_per_len;		/* copy of pte_csh_entries_per_len corresponding to curalt_depth */
GBLDEF	int4		cur_pte_csh_tail_count;			/* copy of pte_csh_tail_count corresponding to curalt_depth */

GBLDEF	readonly char	*before_image_lit[] = {"NOBEFORE_IMAGES", "BEFORE_IMAGES"};
GBLDEF	readonly char	*jnl_state_lit[]    = {"DISABLED", "OFF", "ON"};
GBLDEF	readonly char	*repl_state_lit[]   = {"OFF", "ON", "WAS_ON"};

GBLDEF	boolean_t	crit_sleep_expired;		/* mutex.mar: signals that a timer waiting for crit has expired */
GBLDEF	uint4		crit_deadlock_check_cycle;	/* compared to csa->crit_check_cycle to determine if a given region
							   in a transaction legitimately has crit or not */
GBLDEF	node_local_ptr_t	locknl;		/* if non-NULL, indicates node-local of interest to the LOCK_HIST macro */
GBLDEF	boolean_t	in_mutex_deadlock_check;	/* if TRUE, mutex_deadlock_check() is part of our current C-stack trace */
/* $ECODE and $STACK related variables.
 * error_frame and skip_error_ret should ideally be part of dollar_ecode structure. since sr_avms/opp_ret.m64 uses these
 * global variables and it was felt risky changing it to access a member of a structure, they are kept as separate globals */
GBLDEF	stack_frame		*error_frame;		/* ptr to frame where last error occurred or was rethrown */
GBLDEF	boolean_t		skip_error_ret;		/* set to TRUE by golevel(), used and reset by op_unwind() */
GBLDEF	dollar_ecode_type	dollar_ecode;		/* structure containing $ECODE related information */
GBLDEF	dollar_stack_type	dollar_stack;		/* structure containing $STACK related information */
GBLDEF	boolean_t		ztrap_explicit_null;	/* whether $ZTRAP was explicitly set to NULL in the current frame */
GBLDEF	int4			gtm_object_size;	/* Size of entire gtm object for compiler use */
GBLDEF	int4			linkage_size;		/* Size of linkage section during compile */
GBLDEF	uint4			lnkrel_cnt;		/* number of entries in linkage Psect to relocate */
GBLDEF  int4			sym_table_size;		/* size of the symbol table during compilation */
GBLDEF	boolean_t		stop_non_mandatory_expansion, non_mandatory_expansion; /* Used in stringpool managment */
GBLDEF	jnl_fence_control	jnl_fence_ctl;
GBLDEF	jnl_process_vector	*prc_vec;		/* for current process */
GBLDEF	jnl_process_vector	*originator_prc_vec;	/* for client/originator */
LITDEF	char	*jrt_label[JRT_RECTYPES] =
{
#define JNL_TABLE_ENTRY(rectype, extract_rtn, label, update, fixed_size, is_replicated)	label,
#include "jnl_rec_table.h"	/* BYPASSOK */
#undef JNL_TABLE_ENTRY
};
LITDEF	int	jrt_update[JRT_RECTYPES] =
{
#define JNL_TABLE_ENTRY(rectype, extract_rtn, label, update, fixed_size, is_replicated)	update,
#include "jnl_rec_table.h"	/* BYPASSOK */
#undef JNL_TABLE_ENTRY
};
LITDEF	boolean_t	jrt_fixed_size[JRT_RECTYPES] =
{
#define JNL_TABLE_ENTRY(rectype, extract_rtn, label, update, fixed_size, is_replicated)	fixed_size,
#include "jnl_rec_table.h"	/* BYPASSOK */
#undef JNL_TABLE_ENTRY
};
LITDEF	boolean_t	jrt_is_replicated[JRT_RECTYPES] =
{
#define JNL_TABLE_ENTRY(rectype, extract_rtn, label, update, fixed_size, is_replicated)	is_replicated,
#include "jnl_rec_table.h"	/* BYPASSOK */
#undef JNL_TABLE_ENTRY
};
LITDEF	char	*jnl_file_state_lit[JNL_FILE_STATES] =
{
	"JNL_FILE_UNREAD",
	"JNL_FILE_OPEN",
	"JNL_FILE_CLOSED",
	"JNL_FILE_EMPTY"
};
/* Change the initialization if struct_jrec_tcom in jnl.h changes */
GBLDEF	struct_jrec_tcom	tcom_record = {{JRT_TCOM, TCOM_RECLEN, 0, 0, 0, 0},
					{0}, 0, 0, 0, "", {TCOM_RECLEN, JNL_REC_SUFFIX_CODE}};
GBLDEF	jnl_gbls_t		jgbl;
GBLDEF	short 		crash_count;
GBLDEF	trans_num	start_tn;
GBLDEF	cw_set_element	cw_set[CDB_CW_SET_SIZE];
GBLDEF	unsigned char	cw_set_depth, cw_map_depth;
GBLDEF	unsigned int	t_tries;
GBLDEF	uint4		t_err;
GBLDEF	uint4		update_trans;	/* Bitmask indicating among other things whether this region was updated;
					 * See gdsfhead.h for UPDTRNS_* bitmasks
					 * Bit-0 is 1 if cw_set_depth is non-zero or if it is a duplicate set
					 * 	(cw_set_depth is zero in that case).
					 * Bit-1 is unused for non-TP.
					 * Bit-2 is 1 if transaction commit in this region is beyond point of rollback.
					 */

GBLDEF	boolean_t	is_uchar_wcs_code[] = 	/* uppercase failure codes that imply database cache related problem */
{	/* if any of the following failure codes are seen in the final retry, wc_blocked will be set to trigger cache recovery */
#define	CDB_SC_NUM_ENTRY(code, final_retry_ok, value)
#define CDB_SC_UCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)	is_wcs_code,
#define	CDB_SC_LCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)
#include "cdb_sc_table.h"	/* BYPASSOK */
#undef CDB_SC_NUM_ENTRY
#undef CDB_SC_UCHAR_ENTRY
#undef CDB_SC_LCHAR_ENTRY
};
GBLDEF	boolean_t	is_lchar_wcs_code[] = 	/* lowercase failure codes that imply database cache related problem */
{	/* if any of the following failure codes are seen in the final retry, wc_blocked will be set to trigger cache recovery */
#define	CDB_SC_NUM_ENTRY(code, final_retry_ok, value)
#define CDB_SC_UCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)
#define	CDB_SC_LCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)	is_wcs_code,
#include "cdb_sc_table.h"	/* BYPASSOK */
#undef CDB_SC_NUM_ENTRY
#undef CDB_SC_UCHAR_ENTRY
#undef CDB_SC_LCHAR_ENTRY
};
GBLDEF	boolean_t	is_final_retry_code_num[] = 	/* failure codes that are possible in final retry : numeric */
{
#define	CDB_SC_NUM_ENTRY(code, final_retry_ok, value)			final_retry_ok,
#define CDB_SC_UCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)
#define CDB_SC_LCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)
#include "cdb_sc_table.h"	/* BYPASSOK */
#undef CDB_SC_NUM_ENTRY
#undef CDB_SC_UCHAR_ENTRY
#undef CDB_SC_LCHAR_ENTRY
};
GBLDEF	boolean_t	is_final_retry_code_uchar[] = 	/* failure codes that are possible in final retry : upper case */
{
#define	CDB_SC_NUM_ENTRY(code, final_retry_ok, value)
#define CDB_SC_UCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)	final_retry_ok,
#define CDB_SC_LCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)
#include "cdb_sc_table.h"	/* BYPASSOK */
#undef CDB_SC_NUM_ENTRY
#undef CDB_SC_UCHAR_ENTRY
#undef CDB_SC_LCHAR_ENTRY
};
GBLDEF	boolean_t	is_final_retry_code_lchar[] = 	/* failure codes that are possible in final retry : lower case */
{
#define	CDB_SC_NUM_ENTRY(code, final_retry_ok, value)
#define CDB_SC_UCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)
#define	CDB_SC_LCHAR_ENTRY(code, final_retry_ok, is_wcs_code, value)	final_retry_ok,
#include "cdb_sc_table.h"	/* BYPASSOK */
#undef CDB_SC_NUM_ENTRY
#undef CDB_SC_UCHAR_ENTRY
#undef CDB_SC_LCHAR_ENTRY
};
GBLDEF	boolean_t	gvdupsetnoop = TRUE;	/* if TRUE, duplicate SETs do not change GDS block (and therefore no PBLK journal
						 * records will be written) although the database transaction number will be
						 * incremented and logical SET journal records will be written. By default, this
						 * behavior is turned ON. GT.M has a way of turning it off with a VIEW command.
						 */
GBLDEF  int4		ydb_fullblockwrites;	/* Do full (not partial) 1. file system block writes, or 2. database block writes */
GBLDEF	volatile boolean_t	in_wcs_recover;	/* TRUE if in "wcs_recover", used by "bt_put" and "generic_exit_handler" */
GBLDEF	boolean_t	in_gvcst_incr;		/* set to TRUE by gvcst_incr, set to FALSE by gvcst_put
						 * distinguishes to gvcst_put, if the current db operation is a SET or $INCR */
GBLDEF	mval		*post_incr_mval;	/* mval pointing to the post-$INCR value */
GBLDEF	mval		increment_delta_mval;	/* mval holding the INTEGER increment value, set by gvcst_incr,
						 * used by gvcst_put/gvincr_recompute_upd_array which is invoked by t_end */
GBLDEF	boolean_t	is_dollar_incr;		/* valid only if gvcst_put is in the call-stack (i.e. t_err == ERR_GVPUTFAIL);
						 * is a copy of "in_gvcst_incr" just before it got reset to FALSE */
GBLDEF	int		indir_cache_mem_size;	/* Amount of memory currently in use by indirect cache */
GBLDEF	hash_table_objcode cache_table;
GBLDEF  int		cache_hits, cache_fails;
/* The alignment feature is disabled due to some issues in stringpool garbage collection.
 * TODO: When we sort out stringpool issues, change mstr_native_align to TRUE below */
GBLDEF	boolean_t	mstr_native_align;
GBLDEF boolean_t	save_mstr_native_align;
GBLDEF	mvar		*mvartab;
GBLDEF	mvax		*mvaxtab,*mvaxtab_end;
GBLDEF	mlabel		*mlabtab;
GBLDEF	mline		mline_root;
GBLDEF	mline		*mline_tail;
GBLDEF	triple		t_orig;
GBLDEF	int		mvmax, mlmax, mlitmax;
static	char		routine_name_buff[SIZEOF(mident_fixed)], module_name_buff[SIZEOF(mident_fixed)];
static	char		int_module_name_buff[SIZEOF(mident_fixed)];
GBLDEF	MIDENT_DEF(routine_name, 0, &routine_name_buff[0]);
GBLDEF	MIDENT_DEF(module_name, 0, &module_name_buff[0]);
GBLDEF	MIDENT_DEF(int_module_name, 0, &int_module_name_buff[0]);
GBLDEF	char		rev_time_buf[REV_TIME_BUFF_LEN];
GBLDEF	unsigned short	source_name_len;
GBLDEF	short		object_name_len;
GBLDEF unsigned char	source_file_name[MAX_FBUFF + 1];
GBLDEF unsigned char	object_file_name[MAX_FBUFF + 1];
GBLDEF int		object_file_des;
GBLDEF	int4		curr_addr, code_size;
GBLDEF	mident_fixed	zlink_mname;
GBLDEF	sm_uc_ptr_t	reformat_buffer;
GBLDEF	int		reformat_buffer_len;
GBLDEF	volatile int	reformat_buffer_in_use;	/* used only in DEBUG mode */
GBLDEF	boolean_t	mu_reorg_upgrd_dwngrd_in_prog;	/* TRUE if MUPIP REORG UPGRADE/DOWNGRADE is in progress */
GBLDEF	boolean_t	mu_reorg_nosafejnl;		/* TRUE if NOSAFEJNL explicitly specified */
GBLDEF	trans_num	mu_reorg_upgrd_dwngrd_blktn;	/* tn in blkhdr of current block processed by MUPIP REORG {UP,DOWN}GRADE */
GBLDEF	inctn_opcode_t	inctn_opcode = inctn_invalid_op;
GBLDEF	inctn_detail_t	inctn_detail;			/* holds detail to fill in to inctn jnl record */
GBLDEF	uint4		region_open_count;		/* Number of region "opens" we have executed */
GBLDEF	uint4		ydb_blkupgrade_flag = UPGRADE_IF_NEEDED;	/* by default upgrade only if necessary */
GBLDEF	boolean_t	disk_blk_read;
GBLDEF	boolean_t	ydb_dbfilext_syslog_disable;	/* by default, log every file extension message */
GBLDEF	int4		cws_reorg_remove_index;			/* see mu_swap_blk.c for comments on the need for these two */
GBLDEF	block_id	cws_reorg_remove_array[CWS_REORG_REMOVE_ARRAYSIZE];
GBLDEF	uint4		log_interval;
GBLDEF	uint4		ydb_principal_editing_defaults;	/* ext_cap flags if tt */
GBLDEF	boolean_t	in_repl_inst_edit;		/* used by an assert in repl_inst_read/repl_inst_write */
GBLDEF	boolean_t	in_repl_inst_create;		/* used by repl_inst_read/repl_inst_write */
GBLDEF	boolean_t	holds_sem[NUM_SEM_SETS][NUM_SRC_SEMS];	/* whether a particular replication semaphore is being held
								 * by the current process or not. */
GBLDEF	boolean_t	detail_specified;	/* Set to TRUE if -DETAIL is specified in MUPIP REPLIC -JNLPOOL or -EDITINST */
GBLDEF	boolean_t	in_mupip_ftok;		/* Used by an assert in repl_inst_read */
GBLDEF	uint4		section_offset;		/* Used by PRINT_OFFSET_PREFIX macro in repl_inst_dump.c */
GBLDEF	uint4		mutex_per_process_init_pid;	/* pid that invoked "mutex_per_process_init" */
GBLDEF	boolean_t	ydb_quiet_halt;		/* Suppress FORCEDHALT message */
#ifdef UNICODE_SUPPORTED
/* Unicode line terminators.  In addition to the following
 * codepoints, the sequence CR LF is considered a single
 * line terminator.
 */
LITDEF UChar32 u32_line_term[] =
{
	0x000A,			/* Line Feed */
	0x000D,			/* Carraige Return */
	0x0085,			/* Next Line - EBCDIC mostly */
	0x000C,			/* Form Feed */
	UTF_LINE_SEPARATOR,	/* Line Separator */
	UTF_PARA_SEPARATOR,	/* Paragraph Separator */
	0x0000
};
/* Given the first byte in a UTF-8 representation, the following array returns the total number of bytes in the encoding
 *	00-7F : 1 byte
 *	C2-DF : 2 bytes
 *	E0-EF : 3 bytes
 *	F0-F4 : 4 bytes
 *  All others: 1 byte
 * For details on the UTF-8 encoding see gtm_utf8.h
 */
LITDEF unsigned int utf8_bytelen[] =
{
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 00-1F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 20-3F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 40-5F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 60-7F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* 80-9F */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* A0-BF */
	1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* C0-DF */
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* E0-FF */
};
/* Given the first byte in a UTF-8 representation, the following array returns the number of bytes to follow
 *	00-7F :  0 byte
 *	C2-DF :  1 byte
 *	E0-EF :  2 bytes
 *	F0-F4 :  3 bytes
 *  All others: -1 bytes
 * For details on the UTF-8 encoding see gtm_utf8.h
 */
LITDEF signed int utf8_followlen[] =
{
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 00-1F */
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 20-3F */
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 40-5F */
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 60-7F */
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /* 80-9F */
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /* A0-BF */
	-1,-1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, /* C0-DF */
	 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, /* E0-FF */
};
GBLDEF	gtm_wcswidth_fnptr_t	gtm_wcswidth_fnptr;	/* see comment in gtm_utf8.h about this typedef */
#endif
GBLDEF	uint4			ydb_max_sockets;	/* Maximum sockets per socket device supported by this process */
GBLDEF	d_socket_struct		*newdsocket;		/* Commonly used temp socket area */
GBLDEF	boolean_t		dse_all_dump;		/* TRUE if DSE ALL -DUMP is specified */
GBLDEF	int			socketus_interruptus;	/* How many times socket reads have been interrutped */
GBLDEF	int4			pending_errtriplecode;	/* if non-zero contains the error code to invoke ins_errtriple with */
GBLDEF	uint4	process_id;
GBLDEF	uid_t	user_id = INVALID_UID, effective_user_id = INVALID_UID;
GBLDEF	gid_t	group_id = INVALID_GID, effective_group_id = INVALID_GID;
/* Below is a cached copy of "getpwuid" to try avoid future system calls for the same "uid".
 * Note: We do not initialize all members of the "struct passwd" structure explicitly since the format of this structure
 * differs across the various supported platforms. All we care about is the "pw_uid" and "pw_gid" fields get set to
 * INVALID_UID and INVALID_GID and those are the 3rd and 4th fields of the structure on all supported platforms. All fields
 * after the 4th field will automatically be null-initialized by the compiler for us.
 */
GBLDEF	struct	passwd getpwuid_struct = {NULL, NULL, INVALID_UID, INVALID_GID};
GBLDEF	uint4	image_count;	/* not used in UNIX but defined to preserve VMS compatibility */
GBLDEF  size_t  totalRmalloc;                           /* Total storage currently (real) malloc'd (includes extent blocks) */
GBLDEF  size_t  totalAlloc;                             /* Total allocated (includes allocation overhead but not free space */
GBLDEF  size_t  totalUsed;                              /* Sum of user allocated portions (totalAlloc - overhead) */
GBLDEF	size_t	totalRallocGta;				/* Total storage currently (real) mmap alloc'd */
GBLDEF	size_t  totalAllocGta;                          /* Total mmap allocated (includes allocation overhead but not free space */
GBLDEF	size_t  totalUsedGta;                           /* Sum of "in-use" portions (totalAllocGta - overhead) */
GBLDEF	volatile char		*outOfMemoryMitigation;	/* Cache that we will freed to help cleanup if run out of memory */
GBLDEF	uint4			outOfMemoryMitigateSize;/* Size of above cache (in Kbytes) */
GBLDEF	int 			mcavail;
GBLDEF	mcalloc_hdr 		*mcavailptr, *mcavailbase;
GBLDEF	uint4			max_cache_memsize;	/* Maximum bytes used for indirect cache object code */
GBLDEF	uint4			max_cache_entries;	/* Maximum number of cached indirect compilations */
GBLDEF	void		(*cache_table_relobjs)(void);   /* Function pointer to call cache_table_rebuild() */
GBLDEF	ch_ret_type	(*ht_rhash_ch)();		/* Function pointer to hashtab_rehash_ch */
GBLDEF	ch_ret_type	(*jbxm_dump_ch)();		/* Function pointer to jobexam_dump_ch */
GBLDEF	ch_ret_type	(*stpgc_ch)();			/* Function pointer to stp_gcol_ch */
#ifdef DEBUG
GBLDEF	ch_ret_type	(*t_ch_fnptr)();		/* Function pointer to t_ch */
GBLDEF	ch_ret_type	(*dbinit_ch_fnptr)();		/* Function pointer to dbinit_ch */
#endif
GBLDEF	cache_rec_ptr_t	pin_fail_cr;			/* Pointer to the cache-record that we failed while pinning */
GBLDEF	cache_rec	pin_fail_cr_contents;		/* Contents of the cache-record that we failed while pinning */
GBLDEF	cache_rec_ptr_t	pin_fail_twin_cr;		/* Pointer to twin of the cache-record that we failed to pin */
GBLDEF	cache_rec	pin_fail_twin_cr_contents;	/* Contents of twin of the cache-record that we failed to pin */
GBLDEF	bt_rec_ptr_t	pin_fail_bt;			/* Pointer to bt of the cache-record that we failed to pin */
GBLDEF	bt_rec		pin_fail_bt_contents;		/* Contents of bt of the cache-record that we failed to pin */
GBLDEF	int4		pin_fail_in_crit;		/* Holder of crit at the time we failed to pin */
GBLDEF	int4		pin_fail_wc_in_free;		/* Number of write cache records in free queue when we failed to pin */
GBLDEF	int4		pin_fail_wcs_active_lvl;	/* Number of entries in active queue when we failed to pin */
GBLDEF	int4		pin_fail_ref_cnt;		/* Reference count when we failed to pin */
GBLDEF	int4		pin_fail_in_wtstart;		/* Count of processes in wcs_wtstart when we failed to pin */
GBLDEF	int4		pin_fail_phase2_commit_pidcnt;	/* Number of processes in phase2 commit when we failed to pin */
GBLDEF	zwr_hash_table	*zwrhtab;			/* How we track aliases during zwrites */
GBLDEF	uint4		zwrtacindx;			/* When creating $ZWRTACxxx vars for ZWRite, this holds xxx */
GBLDEF	uint4		tstartcycle;			/* lv_val cycle for tstart operations */
GBLDEF	uint4		lvtaskcycle;			/* lv_val cycle for misc lv_val related tasks */
GBLDEF	int4		SPGC_since_LVGC;			/* stringpool GCs since the last lv_val GC */
GBLDEF	int4		LVGC_interval = MIN_SPGC_PER_LVGC;	/* dead data GC is done every LVGC_interval stringpool GCs */
GBLDEF	lv_xnew_var	*xnewvar_anchor;		/* Anchor for unused lv_xnew_var blocks */
GBLDEF	lv_xnew_ref	*xnewref_anchor;		/* Anchor for unused lv_xnew_ref blocks */
GBLDEF	mval		*alias_retarg;			/* Points to an alias return arg created by a "QUIT *" recorded here so
							 * symtab-unwind logic can find it and modify it if necessary if a
							 * symtab popped during the return and the retarg points to an lv_val
							 * that is going to be destroyed.
							 */
#ifdef DEBUG_ALIAS
GBLDEF	boolean_t	lvamon_enabled;			/* Enable lv_val/alias monitoring */
#endif
GBLDEF	int4		ydb_zlib_cmp_level;		/* zlib compression level specified at process startup */
GBLDEF	int4		repl_zlib_cmp_level;		/* zlib compression level currently in use in replication pipe.
							 * This is a source-server specific variable and is non-zero only
							 * if compression is enabled and works in the receiver server as well.
							 */
GBLDEF	zlib_cmp_func_t		zlib_compress_fnptr;
GBLDEF	zlib_uncmp_func_t	zlib_uncompress_fnptr;
GBLDEF	mlk_stats_t	mlk_stats;			/* Process-private M-lock statistics */
/* Initialized blockalrm, block_ttinout and block_sigsent can be used by all threads */
GBLDEF	boolean_t	blocksig_initialized;		/* set to TRUE when blockalrm and block_sigsent are initialized */
GBLDEF	sigset_t	blockalrm;
GBLDEF	sigset_t	block_ttinout;
GBLDEF	sigset_t	block_sigsent;	/* block all signals that can be sent externally
					  (SIGINT, SIGQUIT, SIGTERM, SIGTSTP, SIGCONT) */
GBLDEF  sigset_t	block_worker;	/* block all signals for use by the linux AIO worker thread, except for a few
					 * fatal signals that can be internally generated inside the thread. This way
					 * any signal externally sent always gets handled by the main process and not
					 * by the worker thread.
					 */
GBLDEF  char            *gtm_core_file;
GBLDEF  char            *gtm_core_putenv;
#ifdef __MVS__
GBLDEF	char		*gtm_utf8_locale_object;
GBLDEF	boolean_t	gtm_tag_utf8_as_ascii = TRUE;
#endif

/* Encryption-related fields. */
LITDEF	char		gtmcrypt_repeat_msg[] = "Please look at prior messages related to encryption for more details";
GBLDEF	char		*gtmcrypt_badhash_size_msg;
GBLDEF	boolean_t	gtmcrypt_initialized;		/* Set to TRUE if gtmcrypt_init() completes successfully */
GBLDEF	char		dl_err[MAX_ERRSTR_LEN];
GBLDEF	mstr		pvt_crypt_buf;			/* Temporary buffer if in-place encryption / decryption is not an option */
LITDEF	gtm_string_t	null_iv = {0, ""};
GBLDEF	uint4		mu_reorg_encrypt_in_prog;	/* Reflects whether MUPIP REORG -ENCRYPT is in progress */
GBLDEF	sgmnt_addrs	*reorg_encrypt_restart_csa;	/* Pointer to the region which caused a transaction restart due to a
							 * concurrent MUPIP REORG -ENCRYPT */

#ifdef DEBUG
/* Following definitions are related to white_box testing */
GBLDEF	boolean_t	ydb_white_box_test_case_enabled;
GBLDEF	int		ydb_white_box_test_case_number;
GBLDEF	int		ydb_white_box_test_case_count;
GBLDEF	int 		gtm_wbox_input_test_case_count; /* VMS allows maximum 31 characters for external identifer */
GBLDEF	boolean_t	stringpool_unusable;		/* Set to TRUE by any function that does not expect any of its function
							 * callgraph to use/expand the stringpool. */
GBLDEF	char		stringpool_unusable_set_at_buf[STRINGPOOL_UNUSABLE_AT_BUFFER_SIZE];
/* Buffer of WHO last set/unset stringpool usable */
GBLDEF	boolean_t	stringpool_unexpandable;	/* Set to TRUE by any function for a small period when it has ensured
							 * enough space in the stringpool so it does not expect any more garbage
							 * collections or expansions.
							 */
GBLDEF	boolean_t	donot_INVOKE_MUMTSTART;		/* Set to TRUE whenever an implicit TSTART is done in gvcst_put/kill as
							 * part of an explicit + trigger update. In this situation, we don't expect
							 * MUM_TSTART macro to be invoked at all (see skip_INVOKE_RESTART below
							 * for description on why this is needed). So we keep this debug-only
							 * flag turned on throughout the gvcst_put/kill. An assert in
							 * mdb_condition_handler (invoked by INVOKE_RESTART macro when it does
							 * an rts_error) checks it never gets invoked while this is set.
							 */
#endif
GBLDEF	boolean_t	block_is_free;			/* Set to TRUE if the caller wants to let t_qread know that the block it is
							 * attempting to read is actually a FREE block
							 */
GBLDEF	int4		gv_keysize;
GBLDEF	gd_addr		*gd_header;
#ifdef GTM_TRIGGER
GBLDEF	int4		gtm_trigger_depth;		/* 0 if no trigger, 1 if inside trigger; 2 if inside nested trigger etc. */
GBLDEF	int4		tstart_trigger_depth;		/* gtm_trigger_depth at the time of the outermost "op_tstart"
							 * (i.e. explicit update). This should be used only if dollar_tlevel
							 * is non-zero as it is not otherwise maintained.
							 */
GBLDEF	uint4		trigger_name_cntr;		/* Counter from which trigger names are constructed */
GBLDEF	boolean_t 	*ztvalue_changed_ptr;		/* -> boolean in current gtm_trigger_parms signaling if ztvalue has
							      been updated */
GBLDEF	boolean_t	ztwormhole_used;		/* TRUE if $ztwormhole was used by trigger code */
GBLDEF	mstr		*dollar_ztname;
GBLDEF	mval		*dollar_ztdata,
			*dollar_ztdelim,
			*dollar_ztoldval,
			*dollar_ztriggerop,
			*dollar_ztupdate,
			*dollar_ztvalue,
			dollar_ztwormhole = DEFINE_MVAL_STRING(MV_STR, 0, 0, 0, NULL, 0, 0),
			dollar_ztslate = DEFINE_MVAL_STRING(MV_STR, 0, 0, 0, NULL, 0, 0);
GBLDEF	int		tprestart_state;		/* When triggers restart, multiple states possible. See tp_restart.h */
GBLDEF	boolean_t	skip_INVOKE_RESTART;		/* set to TRUE if caller of op_tcommit/t_retry does not want it to
							 * use the INVOKE_RESTART macro (which uses an rts_error to trigger
							 * the restart) instead return code. The reason we don't want to do
							 * rts_error is that this is an implicit tstart situation where we
							 * did not do opp_tstart.s so we don't want control to be transferred
							 * using the MUM_TSTART macro by mdb_condition_handler (which assumes
							 * opp_tstart.s invocation).
							 */
GBLDEF	boolean_t	goframes_unwound_trigger;	/* goframes() unwound a trigger base frame during its unwinds */
GBLDEF	symval		*trigr_symval_list;		/* List of availalable symvals for use in (nested) triggers */
GBLDEF	boolean_t	dollar_ztrigger_invoked;	/* $ZTRIGGER() was invoked on at least one region in this transaction */
# ifdef DEBUG
  GBLDEF gv_trigger_t		*gtm_trigdsc_last;	/* For debugging purposes - parms gtm_trigger called with */
  GBLDEF gtm_trigger_parms	*gtm_trigprm_last;
  GBLDEF ch_ret_type		(*ch_at_trigger_init)();	/* Condition handler in effect when gtm_trigger called */
# endif
GBLDEF	boolean_t	explicit_update_repl_state;	/* Initialized just before an explicit update invokes any triggers.
							 * Set to 1 if triggering update is to a replicated database.
							 * Set to 0 if triggering update is to a non-replicated database.
							 * Value stays untouched across nested trigger invocations.
							 */
#endif
GBLDEF	boolean_t	skip_dbtriggers;		/* Set to FALSE by default (i.e. triggers are invoked). Set to TRUE
							 * unconditionally by MUPIP LOAD as it always skips triggers. Also set
							 * to TRUE by journal recovery/update-process only when they encounter
							 * updates done by MUPIP LOAD so they too skip trigger processing. In the
							 * case of update process, this keeps primary/secondary in sync. In the
							 * case of journal recovery, this keeps the db and jnl in sync.
							 */
GBLDEF	boolean_t	expansion_failed;		/* used by string pool when trying to expand */
GBLDEF	boolean_t	retry_if_expansion_fails;	/* used by string pool when trying to expand */

GBLDEF	boolean_t	mupip_exit_status_displayed;	/* TRUE if mupip_exit has already displayed a message for non-zero status.
							 * Used by mur_close_files to ensure some message gets printed in case
							 * of abnormal exit status (in some cases mupip_exit might not have been
							 * invoked but we will still go through mur_close_files e.g. if exit is
							 * done directly without invoking mupip_exit).
							 */
GBLDEF	boolean_t	implicit_trollback;		/* Set to TRUE by OP_TROLLBACK macro before calling op_trollback. Set
							 * to FALSE by op_trollback. Used to indicate op_trollback as to
							 * whether it is being called from generated code (opp_trollback.s)
							 * or from C runtime code.
							 */
#ifdef DEBUG
GBLDEF	boolean_t	ok_to_UNWIND_in_exit_handling;	/* see gtm_exit_handler.c for comments */
GBLDEF	boolean_t	skip_block_chain_tail_check;
GBLDEF	boolean_t	in_mu_rndwn_file;		/* TRUE if we are in mu_rndwn_file (holding standalone access) */
#endif
GBLDEF	char		gvcst_search_clue;
/* The following are replication related global variables. Ideally if we had a repl_gbls_t structure (like jnl_gbls_t)
 * this would be a member in that. But since we don't have one and since we need to initialize this specificially to a
 * non-zero value (whereas usually everything else accepts a 0 default value), this is better kept as a separate global
 * variable instead of inside a global variable structure.
 */
GBLDEF	int4			strm_index = INVALID_SUPPL_STRM;
						/* # of the supplementary stream if one exists.
						 * If this process is an update process running on a supplementary
						 *   replication instance and the journal pool allows updates (that
						 *   is the instance was started as a root primary), the value of
						 *   this variable will be anywhere from 1 to 15 once the receiver
						 *   establishes connection with a non-supplementary source server.
						 * If this process is a mumps process running on a supplementary
						 *   replication instance and the journal pool allows updates (that
						 *   is the instance was started as a root primary), the value of
						 *   this variable will be 0 to indicate this is the local stream.
						 * Otherwise, this variable is set to -1 (INVALID_SUPPL_STRM)
						 * This variable is used by t_end/tp_tend to determine if strm_seqno
						 *   field in the journal record needs to be filled in or not.
						 *   It is filled in only if this variable is 0 to 15.
						 */
GBLDEF  repl_conn_info_t        *this_side, *remote_side;
/* Replication related global variables END */
GBLDEF	seq_num			gtmsource_save_read_jnl_seqno;
GBLDEF	gtmsource_state_t	gtmsource_state = GTMSOURCE_DUMMY_STATE;
GBLDEF	boolean_t	gv_play_duplicate_kills;	/* A TRUE value implies KILLs of non-existent nodes will continue to
							 * write jnl records and increment the db curr_tn even though they don't
							 * touch any GDS blocks in the db (i.e. treat it as a duplicate kill).
							 * Set to TRUE for the update process & journal recovery currently.
							 * Set to FALSE otherwise.
							 */
GBLDEF	boolean_t	donot_fflush_NULL;		/* Set to TRUE whenever we don't want gtm_putmsg to fflush(NULL). BYPASSOK
							 * As of Jan 2012, mu_rndwn_all is the only user of this functionality.
							 */
GBLDEF	boolean_t	jnlpool_init_needed;		/* TRUE if jnlpool_init should be done at database init time (eg., for
							 * anticipatory freeze supported configurations). The variable is set
							 * explicitly by interested commands (eg., MUPIP REORG).
							 */
GBLDEF	char		repl_instfilename[MAX_FN_LEN + 1];	/* save first instance */
GBLDEF	char		repl_inst_name[MAX_INSTNAME_LEN];	/* for syslog */
GBLDEF	gd_addr		*repl_inst_from_gld;		/* if above obtained from directory */
GBLDEF	boolean_t	span_nodes_disallowed; 		/* Indicates whether spanning nodes are not allowed. For example,
							 * they are not allowed for GT.CM OMI and GNP. */
GBLDEF	boolean_t	argumentless_rundown;
GBLDEF	is_anticipatory_freeze_needed_t		is_anticipatory_freeze_needed_fnptr;
GBLDEF	set_anticipatory_freeze_t		set_anticipatory_freeze_fnptr;
GBLDEF	boolean_t	is_jnlpool_creator;
GBLDEF	char		ydb_dist[YDB_PATH_MAX];		/* Value of $ydb_dist env variable */
GBLDEF	boolean_t	ydb_dist_ok_to_use = FALSE;		/* Whether or not we can use $ydb_dist */
GBLDEF	semid_queue_elem	*keep_semids;		/* Access semaphores that should be kept because shared memory is up */
GBLDEF	boolean_t		dmterm_default;		/* Retain default line terminators in the direct mode */
GBLDEF	boolean_t	in_jnl_file_autoswitch;		/* Set to TRUE for a short window inside jnl_file_extend when we are about
							 * to autoswitch; used by jnl_write. */
#ifdef GTM_PTHREAD
GBLDEF	pthread_t	gtm_main_thread_id;		/* ID of the main GT.M thread. */
GBLDEF	boolean_t	gtm_main_thread_id_set;		/* Indicates whether the thread ID is set. This is not set just for a jvm
							 * process but also otherwise. This is necessary now for the linux kernel
							 * interface to AIO.
							 */
GBLDEF	boolean_t	gtm_jvm_process;		/* Indicates whether we are running with JVM or stand-alone. */
#endif
GBLDEF	size_t		ydb_max_storalloc;		/* Maximum that YottaDB allows to be allocated - used for testing */
GBLDEF	boolean_t	ipv4_only;			/* If TRUE, only use AF_INET. Reflects the value of the ydb_ipv4_only
							 * environment variable, so is process wide.
							 */
GBLDEF void (*stx_error_fptr)(int in_error, ...);	/* Function pointer for stx_error() so gtm_utf8.c can avoid pulling
							 * stx_error() into gtmsecshr, and thus just about everything else as well.
							 */
GBLDEF void (*stx_error_va_fptr)(int in_error, va_list args);	/* Function pointer for stx_error() so rts_error can avoid pulling
								 * stx_error() into gtmsecshr, and thus just about everything else
								 * as well.
								 */
GBLDEF void (*show_source_line_fptr)(boolean_t warn);	/* Func pointer for show_source_line() - same purpose as stx_error_fptr */
#ifdef GTM_TLS
GBLDEF	gtm_tls_ctx_t	*tls_ctx;			/* Process private pointer to SSL/TLS context. Any SSL/TLS connections that
							 * the process needs to create will be created from this context.
							 * Currently, SSL/TLS is implemented only for replication, but keep it here
							 * so that it can be used when SSL/TLS support is implemented for GT.M
							 * Socket devices.
							 */
#endif
GBLDEF	lv_val		*active_lv;
GBLDEF	boolean_t	in_prin_gtmio = FALSE;		/* Flag to indicate whether we are processing a GT.M I/O function. */
GBLDEF	boolean_t	err_same_as_out;

GBLDEF	boolean_t	multi_proc_in_use;		/* TRUE => parallel processes active ("gtm_multi_proc"). False otherwise */
GBLDEF	multi_proc_shm_hdr_t	*multi_proc_shm_hdr;	/* Pointer to "multi_proc_shm_hdr_t" structure in shared memory
							 *	created by "gtm_multi_proc".
							 */
GBLDEF	unsigned char	*multi_proc_key;		/* NULL for parent process; Non-NULL for child processes forked off
							 *	in "gtm_multi_proc" (usually a null-terminated pointer to the
							 *	region name)
							 */
#ifdef DEBUG
GBLDEF	boolean_t	multi_proc_key_exception;	/* If TRUE, multi_proc_key can be NULL even if multi_proc_use is TRUE.
							 * If FALSE, multi_proc_key shold be non-NULL if multi_proc_use is TRUE.
							 *	else an assert in util_format will fail.
							 */
#endif
GBLDEF	boolean_t	multi_thread_in_use;		/* TRUE => threads are in use. FALSE => not in use */
GBLDEF	boolean_t	thread_mutex_initialized;	/* TRUE => "thread_mutex" variable is initialized */
GBLDEF	pthread_mutex_t	thread_mutex;			/* mutex structure used to ensure serialization in case we need
							 * to execute some code that is not thread-safe. Note that it is
							 * more typical to use different mutexes for different things that
							 * need concurrency protection, e.g., memory allocation, encryption,
							 * token hash table, message buffers, etc. If the single mutex becomes
							 * a bottleneck this needs to be revisited.
							 */
GBLDEF	pthread_t	thread_mutex_holder;		/* pid/tid of the thread that has "thread_mutex" currently locked */
GBLDEF	pthread_key_t	thread_gtm_putmsg_rname_key;	/* points to region name corresponding to each running thread */
GBLDEF	boolean_t	thread_block_sigsent;		/* TRUE => block external signals SIGINT/SIGQUIT/SIGTERM/SIGTSTP/SIGCONT */
GBLDEF	boolean_t	in_nondeferrable_signal_handler;	/* TRUE if we are inside "generic_signal_handler". Although this
								 * is a dbg-only variable, the GBLDEF needs to stay outside of
								 * a #ifdef DEBUG because this is used inside gtm_malloc_dbg
								 * which is even used by a non-debug mumps link.
								 */
GBLDEF	boolean_t	forced_thread_exit;		/* TRUE => signal threads to exit (likely because some thread already
							 * exited with an error or the main process got a SIGTERM etc.)
							 */
GBLDEF	int		next_task_index;		/* "next" task index waiting for a thread to be assigned */
GBLDEF	int		ydb_mupjnl_parallel;		/* Maximum # of concurrent threads or procs to use in "gtm_multi_thread"
							 *		or in forward phase of mupip recover.
							 *	0 => Use one thread/proc per region.
							 *	1 => Serial execution (no threads)
							 *	2 => 2 threads concurrently run
							 *	etc.
							 * Currently only mupip journal commands use this.
							 */
GBLDEF	boolean_t	ctrlc_on;			/* TRUE in cenable mode; FALSE in nocenable mode */
#ifdef DEBUG
GBLDEF	int		ydb_db_counter_sem_incr;	/* Value used to bump the counter semaphore by every process.
							 * Default is 1. Higher values exercise the ERANGE code better
							 * when the ftok/access/jnlpool counter semaphore overflows.
							 */
GBLDEF	boolean_t	forw_recov_lgtrig_only;		/* TRUE if jgbl.forw_phase_recovery is TRUE AND the current TP transaction
							 * being played consists of only *LGTRIG* records (no SET/KILL etc.)
							 * Used by an assert in op_tcommit.
							 */
GBLDEF	boolean_t	in_mu_cre_file;			/* TRUE only if inside "mu_cre_file" function (used by an assert).
							 * This is MUPIP CREATE only (db does not exist at that point and so
							 * threads are unlikely there) AND is dbg-only and hence okay to be a
							 * gbldef instead of a threadgbldef.
							 */
GBLDEF	enum dbg_wtfini_lcnt_t	dbg_wtfini_lcnt;	/* "lcnt" value for WCS_OPS_TRACE tracking purposes. This is dbg-only
							 * and hence it is okay to be a gbldef instead of a threadgbldef.
							 */
#endif
GBLDEF	sgm_info	*sgm_info_ptr;
GBLDEF	tp_region	*tp_reg_free_list;	/* Ptr to list of tp_regions that are unused */
GBLDEF  tp_region	*tp_reg_list;		/* Ptr to list of tp_regions for this transaction */

#ifdef USE_LIBAIO
GBLDEF  char 		*aio_shim_errstr;	/* If an error occurred (mostly but not limited to EAGAIN),
						 * what triggered it?
						 */
GBLDEF	char		io_setup_errstr[IO_SETUP_ERRSTR_ARRAYSIZE];
							/* The original nr_events used by the client is necessary
							 * to understand why io_setup() failed on occasion. We set
							 * up an error string of the form io_setup(nr_events).
							 */
#endif
GBLDEF	void		(*mupip_exit_fp)(int4 errnum);	/* Function pointer to mupip_exit() in MUPIP but points to a routine
							 * that assert fails if run from non-MUPIP builds.
							 */
GBLDEF	CLI_ENTRY	*cmd_ary;	/* Pointer to command table for MUMPS/DSE/LKE etc. */

/* Begin -- GT.CM OMI related global variables */
GBLDEF	bool		neterr_pending;
GBLDEF	int4		omi_bsent = 0;
GBLDEF	int		psock = -1;		/* pinging socket */
GBLDEF	short		gtcm_ast_avail;
GBLDEF	int4		gtcm_exi_condition;
GBLDEF	char		*omi_service = (char *)0;
GBLDEF	FILE		*omi_debug   = (FILE *)0;
GBLDEF	char		*omi_pklog   = (char *)0;
GBLDEF	char		*omi_pklog_addr = (char *)0;
GBLDEF	int		omi_pkdbg   = 0;
GBLDEF	omi_conn_ll	*omi_conns   = (omi_conn_ll *)0;
GBLDEF	int		omi_exitp   = 0;
GBLDEF	int		omi_pid     = 0;
GBLDEF	int4		omi_errno   = 0;
GBLDEF	int4		omi_nxact   = 0;
GBLDEF	int4		omi_nxact2  = 0;
GBLDEF	int4		omi_nerrs   = 0;
GBLDEF	int4		omi_brecv   = 0;
GBLDEF	int4		gtcm_stime  = 0;  /* start time for GT.CM */
GBLDEF	int4		gtcm_ltime  = 0;  /* last time stats were gathered */
GBLDEF	int		one_conn_per_inaddr = 0;
GBLDEF	int		authenticate = 0;   /* authenticate OMI connections */
GBLDEF	int		ping_keepalive = 0; /* check connections using ping */
GBLDEF	int		conn_timeout = TIMEOUT_INTERVAL;
GBLDEF	int		history = 0;
/* image_id....allows you to determine info about the server by using the strings command, or running dbx */
GBLDEF	char		image_id[256]= "image_id";
/* End -- GT.CM OMI related global variables */

GBLDEF	int		ydb_repl_filter_timeout;	/* # of seconds that source server waits before issuing FILTERTIMEDOUT
							 * error if it sees no response from the external filter program.
							 */
GBLDEF	int4		tstart_gtmci_nested_level;	/* TREF(gtmci_nested_level) at the time of the outermost "op_tstart"
							 * This should be used only if dollar_tlevel is non-zero as it is not
							 * otherwise maintained.
							 */
GBLDEF	uint4		deferred_signal_handling_needed;	/* if non-zero, it means the DEFERRED_SIGNAL_HANDLING_CHECK
								 * macro needs to do some work.
								 */
