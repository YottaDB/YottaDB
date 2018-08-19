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

/********************************************************************************************
 * W A R N I N G  ---  W A R N I N G  --- W A R N I N G --- W A R N I N G --- W A R N I N G *
 *											    *
 * This routine (gtmsecshr) runs as setuid to root to perform various functions on behalf   *
 * of YottaDB processes. Extreme care must be taken to prevent all forms of deceptive	    *
 * access, linking with unauthorized libraries, etc. Same applies to anything it calls.	    *
 *											    *
 * W A R N I N G  ---  W A R N I N G  --- W A R N I N G --- W A R N I N G --- W A R N I N G *
 ********************************************************************************************/


#include "mdef.h"
#include "main_pragma.h"

#include <sys/time.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "gtm_stat.h"
#include "gtm_socket.h"
#include <sys/param.h>
#if !defined(_AIX) && !defined(__linux__) && !defined(__hpux) && !defined(__CYGWIN__) && !defined(__MVS__) && !defined(__APPLE__)
# include <siginfo.h>
#endif
#include "gtm_syslog.h"
#include "gtm_limits.h"
#include "gtm_stdlib.h"
#include "gtm_signal.h"
#include "gtm_sem.h"
#include "gtm_string.h"
#include "gtm_un.h"
#include "gtm_fcntl.h"
#include <errno.h>
#include "gtm_time.h"
#include "gtm_unistd.h"
#include "gtm_stdio.h"
#include "gtm_permissions.h"
#include "gtm_select.h"

#if defined(__MVS__)
# include "gtm_zos_io.h"
#endif
#include "cli.h"
#include "error.h"
#include "io.h"
#include "gtmsecshr.h"
#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "mutex.h"
#include "iosp.h"
#include "gt_timer.h"
#include "gtm_c_stack_trace.h"
#include "eintr_wrappers.h"
#include "eintr_wrapper_semop.h"
#include "gtmimagename.h"
#include "util.h"
#include "send_msg.h"
#include "generic_signal_handler.h"
#include "gtmmsg.h"
#include "have_crit.h"
#include "sig_init.h"
#include "gtmio.h"
#include "common_startup_init.h"
#include "gtm_threadgbl_init.h"
#include "hashtab.h"
#include "fork_init.h"
#include "jnl.h"
#include "repl_msg.h"			/* needed by gtmsource.h */
#include "gtmsource.h"			/* for anticipatory_freeze.h */
#include "anticipatory_freeze.h"
#ifdef UNICODE_SUPPORTED
# include "gtm_icu_api.h"
# include "gtm_utf8.h"
#endif
#include "getjobnum.h"
#include "ydb_chk_dist.h"
#include "ydb_getenv.h"

#ifdef __APPLE__
# include <mach-o/dyld.h>
#endif

#define intent_open		"for open"	/* FLUSH_DB_IPCS_INFO types */
#define intent_close		"for close"

GBLDEF CLI_ENTRY	*cmd_ary = NULL; /* GTMSECSHR does not have any command tables so initialize command array to NULL */

GBLREF	int			gtmsecshr_sockfd;
GBLREF	struct sockaddr_un	gtmsecshr_sock_name;
GBLREF	int			gtmsecshr_sockpath_len;
GBLREF	key_t			gtmsecshr_key;
GBLREF	uint4			process_id;
GBLREF	boolean_t		need_core;
GBLREF	boolean_t		first_syslog;		/* Defined in util_output.c */
GBLREF	char			ydb_dist[YDB_PATH_MAX];
GBLREF	boolean_t		ydb_dist_ok_to_use;

LITREF	char			ydb_release_name[];
LITREF	int4			ydb_release_name_len;

static	volatile int		gtmsecshr_timer_popped;
static	int			gtmsecshr_socket_dir_len;

void clean_client_sockets(char *path);
void gtmsecshr_exit (int exit_code, boolean_t dump);
void gtmsecshr_init(char_ptr_t argv[], char **rundir, int *rundir_len);
void gtmsecshr_timer_handler(void);
void gtmsecshr_signal_handler(int sig, siginfo_t *info, void *context);
ZOS_ONLY(boolean_t gtm_tag_error(char *filename, int realtag, int desiredtag);)

error_def(ERR_ASSERT);
error_def(ERR_BADTAG);
error_def(ERR_DBFILOPERR);
error_def(ERR_DBNOTGDS);
error_def(ERR_GTMASSERT);
error_def(ERR_GTMASSERT2);
error_def(ERR_GTMCHECK);
error_def(ERR_GTMSECSHR);
error_def(ERR_GTMSECSHRBADDIR);
error_def(ERR_GTMSECSHRCHDIRF);
error_def(ERR_GTMSECSHRDMNSTARTED);
error_def(ERR_GTMSECSHRFORKF);
error_def(ERR_GTMSECSHRGETSEMFAIL);
error_def(ERR_GTMSECSHRISNOT);
error_def(ERR_GTMSECSHRNOARG0);
error_def(ERR_GTMSECSHROPCMP);
error_def(ERR_GTMSECSHRRECVF);
error_def(ERR_GTMSECSHRREMFILE);
error_def(ERR_GTMSECSHRREMSEM);
error_def(ERR_GTMSECSHRREMSEMFAIL);
error_def(ERR_GTMSECSHRREMSHM);
error_def(ERR_GTMSECSHRSCKSEL);
error_def(ERR_GTMSECSHRSEMGET);
error_def(ERR_GTMSECSHRSENDF);
error_def(ERR_GTMSECSHRSHMCONCPROC);
error_def(ERR_GTMSECSHRSOCKET);
error_def(ERR_GTMSECSHRSRVFID);
error_def(ERR_GTMSECSHRSRVFIL);
error_def(ERR_GTMSECSHRSSIDF);
error_def(ERR_GTMSECSHRSTART);
error_def(ERR_GTMSECSHRSUIDF);
error_def(ERR_GTMSECSHRTMOUT);
error_def(ERR_GTMSECSHRTMPPATH);
error_def(ERR_GTMSECSHRUPDDBHDR);
error_def(ERR_MEMORY);
error_def(ERR_OUTOFSPACE);
error_def(ERR_STACKOFLOW);
error_def(ERR_SYSCALL);
error_def(ERR_TEXT);

/* Note that this condition handler is not really properly setup as a condition handler
 * in that it has none of the required condition handler macros in it. It's job is just
 * to perform shutdown logic when it is called. No further handlers are called hence
 * the streamlined nature.
 */
CONDITION_HANDLER(gtmsecshr_cond_hndlr)
{
	START_CH(TRUE);
	gtmsecshr_exit(arg, DUMPABLE ? TRUE : FALSE);
}

/* If there was a leftover socket, the client will append a lower case letter
 * which we take as a flag to delete all sockets for the current client pid
 */
void clean_client_sockets(char *path)
{
	char	last, suffix;
	int	len;

	len = STRLEN(path);
	last = path[len - 1];
	for (suffix = 'a'; last > suffix; suffix++)
	{
		path[len - 1] = suffix;		/* OK since main loop is done with this */
		UNLINK(path);			/* We could care less about errors */
	}
	path[len - 1] = '\0';			/* The normal name for the pid */
	UNLINK(path);
	return;
}

/* For gtmsecshr, override this routine since crit doesn't exist here */
uint4 have_crit(uint4 crit_state)
{
	return 0;
}
/* For gtmsecshr, no forking but still allow to create a core */
void gtm_fork_n_core(void)
{	/* Should we switch to an otherwise unpriv'd id of some sort and chdir to create the core
	 * in say $ydb_dist ?
	 */
	DUMP_CORE;
}

/* Main gtmsecshr entry point.
 *
 * Note the functions in util_output know the caller is gtmsecshr so ALL messages flushed through it
 * automatically go to syslog. There is no flat-file logging.
 */
int main(int argc, char_ptr_t argv[])
{
	int			selstat;
	int			save_errno;
	int			recv_complete, send_complete;
	int			num_chars_recd, num_chars_sent, rundir_len;
	TID			timer_id;
	GTM_SOCKLEN_TYPE	client_addr_len;
	char			*recv_ptr, *send_ptr, *rundir;
	fd_set			wait_on_fd;
	struct sockaddr_un	client_addr;
	struct timeval		input_timeval;
	gtmsecshr_mesg		mesg;
	DCL_THREADGBL_ACCESS;

	GTM_THREADGBL_INIT;
	assert(MAXPOSINT4 >= GTMSECSHR_MESG_TIMEOUT);
	common_startup_init(GTMSECSHR_IMAGE, NULL); /* Side-effect : Sets skip_dbtriggers = TRUE if platorm lacks trigger support */
	err_init(gtmsecshr_cond_hndlr);
	gtmsecshr_init(argv, &rundir, &rundir_len);
	timer_id = (TID)main;
	while (TRUE)
	{
		input_timeval.tv_sec = MAX_TIMEOUT_VALUE;	/* Restart timeout each interation for platforms that save
								 * unexpired time when select exits.
								 */
		input_timeval.tv_usec = 0;
		assertpro(FD_SETSIZE > gtmsecshr_sockfd);
		FD_ZERO(&wait_on_fd);
		FD_SET(gtmsecshr_sockfd, &wait_on_fd);
		gtmsecshr_timer_popped = FALSE;
		SELECT(gtmsecshr_sockfd + 1, (void *)&wait_on_fd, NULL, NULL, &input_timeval, selstat);
		if (0 > selstat)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_GTMSECSHR, 1, process_id, ERR_GTMSECSHRSCKSEL, 0, errno);
		else if (0 == selstat)
		{
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_GTMSECSHRTMOUT);
			gtmsecshr_exit(0, 0);	/* Doesn't return */
		}
		recv_ptr = (char *)&mesg;
		client_addr_len = SIZEOF(struct sockaddr_un);
		DBGGSSHR((LOGFLAGS, "gtmsecshr: Select rc = %d  message timeout = %d\n", selstat, GTMSECSHR_MESG_TIMEOUT));
		start_timer(timer_id, GTMSECSHR_MESG_TIMEOUT, gtmsecshr_timer_handler, 0, NULL);
		recv_complete = FALSE;
		do
		{	/* Note RECVFROM does not loop on EINTR return codes so must be handled */
			num_chars_recd = (int)(RECVFROM(gtmsecshr_sockfd, (void *)recv_ptr, SIZEOF(mesg), 0,
							(struct sockaddr *)&client_addr, &client_addr_len));
			save_errno = errno;
			DBGGSSHR((LOGFLAGS, "gtmsecshr: timer-popped: %d  RECVFROM rc = %d  errno = %d (only relevant if rc = "
				  "-1)\n", gtmsecshr_timer_popped, num_chars_recd, save_errno));
			if ((0 >= num_chars_recd) && (gtmsecshr_timer_popped || EINTR != save_errno))
				/* Note error includes 0 return from UDP read - should never be possible with UDP */
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_GTMSECSHR, 1,
					process_id, ERR_GTMSECSHRRECVF, 0, save_errno);
			if (0 < num_chars_recd)
				recv_complete = TRUE;	/* Only complete messages received via UDP datagram */
		} while (!recv_complete);
		cancel_timer(timer_id);
		assert(0 < num_chars_recd);
		service_request(&mesg, num_chars_recd, rundir, rundir_len);
		DBGGSSHR((LOGFLAGS, "gtmsecshr: service request complete - return code: %d\n", mesg.code));
		if (INVALID_COMMAND != mesg.code)
		{	/* Reply if code not overridden to mean no acknowledgement required */
			send_ptr = (char *)&mesg;
			start_timer(timer_id, GTMSECSHR_MESG_TIMEOUT, gtmsecshr_timer_handler, 0, NULL);
			send_complete = FALSE;
			do
			{
				num_chars_sent = (int)(SENDTO(gtmsecshr_sockfd, send_ptr, GTM_MESG_HDR_SIZE, 0,
							      (struct sockaddr *)&client_addr, (GTM_SOCKLEN_TYPE)client_addr_len));
				save_errno = errno;
				DBGGSSHR((LOGFLAGS, "gtmsecshr: timer-popped: %d  SENDTO rc = %d  errno = %d (only relevant if "
					  "rc = -1)\n", gtmsecshr_timer_popped, num_chars_recd, save_errno));
				if ((0 >= num_chars_sent) && (gtmsecshr_timer_popped || save_errno != EINTR))
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_GTMSECSHR, 1,
						process_id, ERR_GTMSECSHRSENDF, 0, save_errno);
				if (0 < num_chars_sent)
					send_complete = TRUE;
			} while (!send_complete);
			cancel_timer(timer_id);
		} else
			DBGGSSHR((LOGFLAGS, "gtmsecshr: SENDTO reply skipped due to mesg.code = INVALID_COMMAND\n"));
		assert(('a' > 'F') && ('a' > '9'));
		if ('a' <= client_addr.sun_path[strlen(client_addr.sun_path) - 1])
			clean_client_sockets(client_addr.sun_path);
	}
}

/* Note: *rundir points to $ydb_dist (the full path of ydb_dist env var) on return */
void gtmsecshr_init(char_ptr_t argv[], char **rundir, int *rundir_len)
{
	int		file_des, save_errno, len = 0;
	int		create_attempts = 0;
	int		secshr_sem;
	int		semop_res, rndirln, modlen, ydbdistlen;
	char		*name_ptr, *rndir, *tmp_ptr, ydbdist[YDB_PATH_MAX];
	char		gtmsecshr_realpath[YDB_PATH_MAX];
	char		*path, *chrrv;
	pid_t		pid;
	struct sembuf	sop[4];
	gtmsecshr_mesg	mesg;
	struct stat	stat_buf;
	char		*ydb_tmp_ptr;
	int		status;
	int		lib_gid;
	struct stat	dist_stat_buff;
	intrpt_state_t	prev_intrpt_state;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	/* Before priv escalation need to understand how/where we were invoked in terms of module path and name.
	 *
	 * Steps:
	 *
	 * 1. Verify the currently running executable is "$ydb_dist/gtmsecshrdir/gtmsecshr". Issue error otherwise.
	 * 2. Verify current working dir is $ydb_dist/gtmsecshrdir. Issue error otherwise.
	 * 3. Copy "$ydb_dist" to output parameter "*rundir"
	 *
	 * Note when/if this module merges with its wrapper, step 2 should be modified to that extent. Note our dependence
	 * on argv[0] for correct startup directory may preclude getting rid of the wrapper. The execl*() functions allow
	 * an invocation dir to be specified that has little to nothing to do with the actual invocation.
	 */

	/* Step 1 */
	#if defined(__APPLE__)
	uint32_t size = YDB_PATH_MAX;
	char gtmsecshr_path[YDB_PATH_MAX];
	if (_NSGetExecutablePath(gtmsecshr_path, &size) < 0)
	{
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_GTMSECSHRSTART, 3,
			RTS_ERROR_LITERAL("Server 1"), process_id, ERR_GTMSECSHRNOARG0);
		gtmsecshr_exit(UNABLETODETERMINEPATH, FALSE);
	}
	rndir = realpath(gtmsecshr_path, gtmsecshr_realpath);
	#elif defined(__linux__)
	rndir = realpath(PROCSELF, gtmsecshr_realpath);
	#endif
	if (NULL != rndir)
		rndirln = STRLEN(rndir);
	else
		rndirln = 0;
	if (0 == rndirln)
	{
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_GTMSECSHRSTART, 3,
			RTS_ERROR_LITERAL("Server 1"), process_id, ERR_GTMSECSHRNOARG0);
		gtmsecshr_exit(UNABLETODETERMINEPATH, FALSE);
	}
	path = ydb_dist;
	chrrv = realpath(path, ydbdist);
	if (NULL != chrrv)
	{
		ydbdistlen = STRLEN(ydbdist);
		/* Now compute the length of the string "$ydb_dist/gtmsecshrdir/gtmsecshr" */
		if (SIZEOF(ydbdist) >= (ydbdistlen + STR_LIT_LEN(GTMSECSHR_DIR_SUFFIX) + 1 + SIZEOF(GTMSECSHR_EXECUTABLE)))
		{
			tmp_ptr = ydbdist + ydbdistlen;
			memcpy(tmp_ptr, GTMSECSHR_DIR_SUFFIX, STR_LIT_LEN(GTMSECSHR_DIR_SUFFIX));
			tmp_ptr += STR_LIT_LEN(GTMSECSHR_DIR_SUFFIX);
			*tmp_ptr++ = '/';
			memcpy(tmp_ptr, GTMSECSHR_EXECUTABLE, STR_LIT_LEN(GTMSECSHR_EXECUTABLE));
			tmp_ptr += STR_LIT_LEN(GTMSECSHR_EXECUTABLE);
			*tmp_ptr = '\0';
			ydbdistlen = tmp_ptr - ydbdist;
		} else
			ydbdistlen = 0;
	} else
		ydbdistlen = 0;
	if ((rndirln != ydbdistlen) || (0 != memcmp(rndir, ydbdist, rndirln)))
	{
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_GTMSECSHRSTART, 3,
			RTS_ERROR_LITERAL("Server 2"), process_id, ERR_GTMSECSHRBADDIR);
		gtmsecshr_exit(BADYDBDISTDIR, FALSE);
	}
	/* Step 2 */
	/* Take off the "/gtmsecshr" suffix (leaves "$ydb_dist/gtmsecshrdir") and use this to compare current working directory */
	rndirln -= SIZEOF(GTMSECSHR_EXECUTABLE); /* SIZEOF includes 1 for trailing null byte but we use that for leading '/') */
	rndir[rndirln] = '\0';			/* Terminate directory string (executable/dir name already checked) */
	chrrv = getcwd(ydbdist, YDB_PATH_MAX);	/* Use ydbdist 'cause it's convenient */
	if (NULL != chrrv)
		ydbdistlen = STRLEN(ydbdist);
	else
		ydbdistlen = 0;
	if ((rndirln != ydbdistlen) || (0 != memcmp(rndir, ydbdist, rndirln)))
	{
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_GTMSECSHRSTART, 3,
			RTS_ERROR_LITERAL("Server 3"), process_id, ERR_GTMSECSHRBADDIR);
		gtmsecshr_exit(BADYDBDISTDIR, FALSE);
	}
	/* Step 3 */
	rndirln -= STR_LIT_LEN(GTMSECSHR_DIR_SUFFIX);
	rndir[rndirln] = '\0';			/* Terminate directory string (executable/dir name already checked) */
	tmp_ptr = malloc(rndirln + 1);		/* Malloc space as this dir is used later during message validations */
	memcpy(tmp_ptr, rndir, rndirln);
	tmp_ptr[rndirln] = '\0';			/* Remove gtmsecshrdir part - Put it back to supplied path ($ydb_dist) */
	*rundir = tmp_ptr;				/* This value for $ydb_dist is used in later validations */
	*rundir_len = rndirln;				/* .. and can be used either with this len or NULL char terminator */
	/*
	 **** With invocation validated, begin our priviledge escalation ****
	 */
	if (-1 == setuid(ROOTUID))
	{
		save_errno = errno;
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(10) MAKE_MSG_WARNING(ERR_GTMSECSHRSTART), 3,
			RTS_ERROR_LITERAL("Server 4"), process_id, ERR_GTMSECSHRSUIDF, 0, ERR_GTMSECSHROPCMP, 0, save_errno);
		gtmsecshr_exit(SETUIDROOT, FALSE);
	}
	/* Before we fork, close the system log because when the facility name disappears in this middle-process,
	 * the logging capability disappears on some systems too - On others, it takes the executable name instead.
	 * Either one causes our tests to fail.
	 */
	DEFER_INTERRUPTS(INTRPT_IN_LOG_FUNCTION, prev_intrpt_state);
	CLOSELOG();
	ENABLE_INTERRUPTS(INTRPT_IN_LOG_FUNCTION, prev_intrpt_state);
	first_syslog = TRUE;
	FORK(pid);
	if (0 > pid)
	{	/* Fork failed */
		save_errno = errno;
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_GTMSECSHRSTART, 3,
			RTS_ERROR_LITERAL("Server 5"), process_id, ERR_GTMSECSHRFORKF, 0, save_errno);
		EXIT(GNDCHLDFORKFLD);
	} else if (0 < pid)
		/* This is the original process - it dies quietly (no exit handler of any sort) to isolate us */
		UNDERSCORE_EXIT(EXIT_SUCCESS);
	/****** We are now in the (isolated) child process ******/
	getjobnum();
	pid = getsid(process_id);
	ydb_dist_ok_to_use = TRUE;
	if ((pid != process_id) && ((pid_t)-1 == setsid()))
	{
		save_errno = errno;
		DEBUG_ONLY(util_out_print("expected sid !UL but have !UL", OPER, process_id, pid));
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) MAKE_MSG_WARNING(ERR_GTMSECSHRSTART), 3,
			RTS_ERROR_LITERAL("Server 6"), process_id, ERR_GTMSECSHRSSIDF, 0, save_errno);
	}
	/* Close standard IO devices - we don't need/want them as all IO goes to operator log. Else we would have IO devices
	 * tied to whatever process started gtmsecshr up which we definitely don't want.
	 */
	CLOSEFILE(0, status);	/* No stdin */
	CLOSEFILE(1, status);	/* No stdout */
	CLOSEFILE(2, status);	/* No stderr */
	/* Init signal handling which works slightly different than in other utilities - gtmsecshr has its own handler which
	 * *calls* generic_signal_handler (which always returns for gtmsecshr) - we then drive our normal exit handling.
	 */
	sig_init(gtmsecshr_signal_handler, NULL, NULL, NULL);
	file_des = sysconf(_SC_OPEN_MAX);
	for (file_des = file_des - 1; file_des >= 3; file_des--)
	{	/* Close the file only if we have it open. This is to avoid a CLOSEFAIL error in case of
		 * trying to close an invalid file descriptor.
		 */
		CLOSEFILE_IF_OPEN(file_des, status);
	}
	if (-1 == CHDIR(P_tmpdir))	/* Switch to temporary directory as CWD */
	{
		save_errno = errno;
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(10) MAKE_MSG_SEVERE(ERR_GTMSECSHRSTART), 3,
			RTS_ERROR_LITERAL("Server 7"), process_id, ERR_GTMSECSHRCHDIRF, 2, LEN_AND_STR(P_tmpdir), save_errno);
		EXIT(UNABLETOCHDIR);
	}
	umask(0);
	if (0 != gtmsecshr_pathname_init(SERVER, *rundir, *rundir_len))
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) MAKE_MSG_SEVERE(ERR_GTMSECSHRSOCKET), 3, RTS_ERROR_LITERAL("Server path"),
			process_id);
	if (-1 == (secshr_sem = semget(gtmsecshr_key, FTOK_SEM_PER_ID, RWDALL | IPC_NOWAIT)))
	{
		secshr_sem = INVALID_SEMID;
		if ((ENOENT != errno) ||		/* Below will fail otherwise */
		    (-1 == (secshr_sem = semget(gtmsecshr_key, FTOK_SEM_PER_ID, RWDALL | IPC_CREAT | IPC_NOWAIT | IPC_EXCL))))
		{
			secshr_sem = INVALID_SEMID;
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_GTMSECSHRSEMGET, 1, errno);
			gtmsecshr_exit(SEMGETERROR, FALSE);
		}
	}
	sop[0].sem_num = 0;
	sop[0].sem_op = 0;
	sop[0].sem_flg  = IPC_NOWAIT;
	sop[1].sem_num = 0;
	sop[1].sem_op = 1;
	sop[1].sem_flg  = IPC_NOWAIT | SEM_UNDO;
	SEMOP(secshr_sem, sop, 2, semop_res, NO_WAIT);
	if (0 > semop_res)
	{
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(10) MAKE_MSG_WARNING(ERR_GTMSECSHRSTART), 3,
			RTS_ERROR_LITERAL("Server 8"), process_id, ERR_TEXT, 2,
			RTS_ERROR_LITERAL("server already running"), errno);
		/* If ydb_tmp is not defined, show default path */
		if (ydb_tmp_ptr = ydb_getenv(YDBENVINDX_TMP_ONLY, NULL_SUFFIX, NULL_IS_YDB_ENV_MATCH))	/* Warning - assignment */
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_GTMSECSHRTMPPATH, 2,
				RTS_ERROR_TEXT(ydb_tmp_ptr), ERR_TEXT, 2, RTS_ERROR_TEXT("(from $ydb_tmp/$gtm_tmp)"));
		else
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_GTMSECSHRTMPPATH, 2, RTS_ERROR_TEXT("/tmp"));
		gtmsecshr_exit(SEMAPHORETAKEN, FALSE);
	}
	if (0 != gtmsecshr_sock_init(SERVER))
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) MAKE_MSG_SEVERE(ERR_GTMSECSHRSOCKET), 3, RTS_ERROR_LITERAL("Server 9"),
			process_id);
	if (-1 == Stat(gtmsecshr_sock_name.sun_path, &stat_buf))
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(10) MAKE_MSG_WARNING(ERR_GTMSECSHRSTART), 3,
			RTS_ERROR_LITERAL("Server 10"), process_id, ERR_TEXT, 2,
			RTS_ERROR_LITERAL("Unable to get status of socket file"), errno);
	/* Get the distribution group */
	lib_gid = gtm_get_group_id(&dist_stat_buff);
	/* If it is world accessible then make mode 666 */
	if ((-1 != lib_gid) && (dist_stat_buff.st_mode & 04))
		lib_gid = -1;
	if (-1 == lib_gid)
		stat_buf.st_mode = 0666;
	else
	{
		/* Change group if different from current user group */
		if (lib_gid != GETGID() && (-1 == CHOWN(gtmsecshr_sock_name.sun_path, -1, lib_gid)))
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(10) MAKE_MSG_WARNING(ERR_GTMSECSHRSTART), 3,
				 RTS_ERROR_LITERAL("Server 11"), process_id, ERR_TEXT, 2,
				 RTS_ERROR_LITERAL("Unable to change socket file group"), errno);
		stat_buf.st_mode = 0660;
	}
	if (-1 == CHMOD(gtmsecshr_sock_name.sun_path, stat_buf.st_mode))
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(10) MAKE_MSG_WARNING(ERR_GTMSECSHRSTART), 3,
			RTS_ERROR_LITERAL("Server 12"), process_id, ERR_TEXT, 2,
			RTS_ERROR_LITERAL("Unable to change socket file permisions"), errno);
	name_ptr = strrchr(gtmsecshr_sock_name.sun_path, '/');
	while (*name_ptr == '/')	/* back off in case of double-slash */
		name_ptr--;
	gtmsecshr_socket_dir_len = (int)(name_ptr - gtmsecshr_sock_name.sun_path + 1);
	/* Preallocate some timer blocks. */
	prealloc_gt_timers();	/* Note we do NOT call gt_timers_add_safe_hndlrs here - don't need them or their baggage */
	/* Create communication key used in all gtmsecshr messages. Key's purpose is to eliminate cross-version
	 * communication issues.
	 */
	STR_HASH((char *)ydb_release_name, ydb_release_name_len, TREF(gtmsecshr_comkey), 0);
	/* Initialization complete */
	send_msg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_GTMSECSHRDMNSTARTED, 5,
		gtmsecshr_key, ydb_release_name_len, ydb_release_name, *rundir_len, *rundir);
	return;
}

void gtmsecshr_exit(int exit_code, boolean_t dump)
{
	int		gtmsecshr_sem;

	if (dump)
		DUMP_CORE;
	gtmsecshr_sock_cleanup(SERVER);
	gtmsecshr_sockfd = FD_INVALID;
	if (SEMAPHORETAKEN != exit_code)
	{	/* remove semaphore */
		if (-1 == (gtmsecshr_sem = semget(gtmsecshr_key, FTOK_SEM_PER_ID, RWDALL | IPC_NOWAIT)))
		{
			gtmsecshr_sem = INVALID_SEMID;
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_GTMSECSHRGETSEMFAIL, 1, errno);
		}
		if (-1 == semctl(gtmsecshr_sem, 0, IPC_RMID, 0))
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_GTMSECSHRREMSEMFAIL, 1, errno);
	}
	/* Note shutdown message taken care of by generic_signal_handler */
	EXIT(exit_code);
}

void gtmsecshr_timer_handler(void)
{
	gtmsecshr_timer_popped = TRUE;
}

void gtmsecshr_signal_handler(int sig, siginfo_t *info, void *context)
{
	/* Do standard signal handling */
	(void)generic_signal_handler(sig, info, context);
	/* Note that we are not letting process_signal run gtm_fork_n_core. In testing,
	 * bad things occurred when the root process ran into trouble trying to fork
	 * so our object is to avoid the problem entirely and core here if we need to
	 * and if the OS will do it (Linux seems not to core if root process).
	 */
	gtmsecshr_exit(sig, need_core);
}

void service_request(gtmsecshr_mesg *buf, int msglen, char *rundir, int rundir_len)
{
	int			flags, fn_len, index, basind, save_errno, save_code;
	int			stat_res, fd;
	char			*basnam, *fn;
	struct shmid_ds		temp_shmctl_buf;
	struct stat		statbuf;
	sgmnt_data		header, *csd;
	endian32_struct		check_endian;
	char			*intent, *buff;
	boolean_t		fd_opened_with_o_direct;
	uint4			fsb_size;
	ZOS_ONLY(int		realfiletag;)
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	save_code = buf->code;
	/* First up, validate the communication key for this gtmsecshr version */
	if (TREF(gtmsecshr_comkey) != buf->comkey)
	{
		buf->code = INVALID_COMKEY;
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(12) ERR_GTMSECSHRSRVFID, 6,
			RTS_ERROR_LITERAL("Server 13"), process_id, buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
			RTS_ERROR_LITERAL("Comkey not correct for this gtmsecshr version"));
		return;
	}
	/* Process code (with code specific valications) */
	switch(buf->code)
	{
#		ifdef NOT_CURRENTLY_USED
		/* Currently, M LOCK wakeup logic is disabled in the client and here. This code was not being used due to a
		 * requirement (and check) that crit was not being held. Since mlk_unlock()->mlk_wake_pending->crit_wake()
		 * runs in crit, this prevented gtmsecshr from EVER being called with this message. This code is disabled until
		 * the next gtmsecshr improvement phase which will modify M unLOCK logic to drive the wakeups only after the
		 * process has released crit. This will restore fast M LOCK wakeups for processes with a different userid than
		 * the current process which currently waits until a 100ms poll timer expires to retry the lock.
		 */
		case WAKE_MESSAGE:
			/* if (0 != validate_receiver(buf, rundir, rundir_len, save_code))
				return;		/ * buf->code already set - to be re-enabled when routine is completed */
			buf->code = 0;
			if ((-1 == kill((pid_t)buf->mesg.id, SIGALRM)) && (ESRCH != errno))
			{
				save_errno = errno;
				buf->code = save_errno;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					     RTS_ERROR_LITERAL("Server 14"), process_id, buf->pid, save_code, buf->mesg.id,
					     ERR_TEXT, 2, RTS_ERROR_LITERAL("Unable to wake up process"), save_errno);
			}
#			ifdef DEBUG
			else
				/* Output msg only in DEBUG mode as these could otherwise be frequent */
				util_out_print("[client pid !UL] Process !UL was awakened", OPER, buf->pid, buf->mesg.id);
#			endif
			break;
#		endif
		case CONTINUE_PROCESS:
			/* if (0 != validate_receiver(buf, rundir, rundir_len, save_code))
				return;		/ * buf->code already set  - to be re-enabled when routine is completed */
			buf->code = 0;
			if ((-1 == kill((pid_t)buf->mesg.id, SIGCONT)) && (ESRCH != errno))
			{
				save_errno = errno;
				buf->code = save_errno;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					     RTS_ERROR_LITERAL("Server 15"), process_id, buf->pid, save_code, buf->mesg.id,
					     ERR_TEXT, 2,
					     RTS_ERROR_LITERAL("Unable to request process to resume processing"),
					     save_errno);
			}
#			ifdef DEBUG
			else
				/* Output msg only in DEBUG mode as these could otherwise be frequent */
				util_out_print("[client pid !UL] Process !UL was requested to resume processing", OPER,
					       buf->pid, buf->mesg.id);
#			endif
			break ;
		case REMOVE_SEM:
			buf->code = (-1 == semctl((int)buf->mesg.id, 0, IPC_RMID, 0)) ? errno : 0;
			if (!buf->code)
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_GTMSECSHRREMSEM, 2, buf->pid, buf->mesg.id);
			else
			{
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 16"), process_id, buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
					RTS_ERROR_LITERAL("Unable to remove semaphore"), buf->code);
			}
			break;
		case REMOVE_SHM:
			buf->code = (-1 == shmctl((int)buf->mesg.id, IPC_STAT, &temp_shmctl_buf)) ? errno : 0;
			if (buf->code)
			{
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 17"), process_id, buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
					RTS_ERROR_LITERAL("Unable to get shared memory statistics"), buf->code);
				break;
			} else if (1 < temp_shmctl_buf.shm_nattch)
			{
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_GTMSECSHRSHMCONCPROC, 2,
					buf->mesg.id, temp_shmctl_buf.shm_nattch);
				buf->code = EBUSY;
				break;
			}
			buf->code = (-1 == shmctl((int)buf->mesg.id, IPC_RMID, 0)) ? errno : 0;
			if (!buf->code)
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_GTMSECSHRREMSHM, 3,
					buf->pid, buf->mesg.id, temp_shmctl_buf.shm_nattch);
			else
			{
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 18"), process_id, buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
					RTS_ERROR_LITERAL("Unable to remove shared memory segment"), buf->code);
			}
			break;
#		ifndef MUTEX_MSEM_WAKE
		case REMOVE_FILE :
			/* This case only exists for plastforms using socket based long sleep when waiting for the
			 * critical section. The memory semaphore based sleeps do not use so cannot orphan this socket.
			 */
			for (index = 0; index < SIZEOF(buf->mesg.path); index++)
			{	/* Verify has null terminator within the message */
				if ('\0' == buf->mesg.path[index])
					break;
			}
			if ((0 == index) || (index >= SIZEOF(buf->mesg.path)) || (index != (msglen - GTM_MESG_HDR_SIZE - 1)))
			{
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFIL, 7,
					RTS_ERROR_LITERAL("Server 19"), process_id, buf->pid, buf->code,
					index >= SIZEOF(buf->mesg.path) ? SIZEOF(buf->mesg.path) - 1 : index,
					buf->mesg.path, ERR_TEXT, 2,
					RTS_ERROR_LITERAL("no file name or length too long or invalid"));
				buf->code = EINVAL;
			} else
			{
				if (('/' == buf->mesg.path[index - 1]) && (1 < index))
				{	/* remove trailing slash unless only slash */
					buf->mesg.path[index - 1] = '\0';
					index--;
				}
				for (basind = index - 1; 0 <= basind; basind--)
				{
					if ('/' == buf->mesg.path[basind])
						break;
				}
				if ((0 < basind) || ((0 == basind) && ('/' == buf->mesg.path[basind])))
					basnam = &buf->mesg.path[basind + 1];
				else
					basnam = &buf->mesg.path[0];
				/* Verify:
				 *
				 * 1. File exists.
				 * 2. File is a socket.
				 * 3. Has a name prefix GT.M would use
				 * 4. File is resident in expected directory ($ydb_tmp or /tmp).
				 *
				 */
				STAT_FILE(buf->mesg.path, &statbuf, stat_res);
				if (-1 == stat_res)
				{
#					ifdef DEBUG
					if (buf->usesecshr && (ENOENT == errno))
						/* ALL unlinks for the mutex socket come thru here in this mode so if socket does
						 * not exist, just let it go.
						 */
						buf->code = 0;
					else
#					endif
					{
						save_errno = errno;
						buf->code = save_errno;
						send_msg_csa(CSA_ARG(NULL) VARLSTCNT(14) ERR_GTMSECSHRSRVFIL, 7,
							RTS_ERROR_LITERAL("Server 20"), process_id, buf->pid, buf->code,
							index, buf->mesg.path, ERR_TEXT, 2,
							RTS_ERROR_LITERAL("Unable to get file status"), save_errno);
					}
				} else if ((!S_ISSOCK(statbuf.st_mode)) ZOS_ONLY(|| (S_ISCHR(statbuf.st_mode))))
				{
					buf->code = EINVAL;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFIL, 7,
						RTS_ERROR_LITERAL("Server 21"), process_id, buf->pid, buf->code,
						index, buf->mesg.path, ERR_TEXT, 2,
						RTS_ERROR_LITERAL("File is not a GTM mutex socket file"));
				} else if (0 != MEMCMP_LIT(basnam, MUTEX_SOCK_FILE_PREFIX))
				{
					buf->code = EINVAL;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFIL, 7,
						RTS_ERROR_LITERAL("Server 22"), process_id, buf->pid, buf->code,
						index, buf->mesg.path, ERR_TEXT, 2,
						RTS_ERROR_LITERAL("File name does not match the naming convention for a GT.M "
								   "mutex socket file"));
				} else if (0 != memcmp(gtmsecshr_sock_name.sun_path, buf->mesg.path, gtmsecshr_socket_dir_len))
				{
					buf->code = EINVAL;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFIL, 7,
						RTS_ERROR_LITERAL("Server 23"), process_id, buf->pid, buf->code,
						index, buf->mesg.path, ERR_TEXT, 2,
						RTS_ERROR_LITERAL("File does not reside in the normal directory for a GT.M "
								     "mutex socket file"));
				} else if (buf->code = (-1 == UNLINK(buf->mesg.path)) ? errno : 0)
				{
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(14) ERR_GTMSECSHRSRVFIL, 7,
						RTS_ERROR_LITERAL("Server 24"), process_id, buf->pid, save_code,
						RTS_ERROR_STRING(buf->mesg.path), ERR_TEXT, 2,
						RTS_ERROR_LITERAL("Unable to remove file"), buf->code);
				} else
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_GTMSECSHRREMFILE, 3,
						buf->pid, RTS_ERROR_STRING(buf->mesg.path));
			}
			break;
#		endif
		case FLUSH_DB_IPCS_INFO:
			/* Most of this code came from file_head_read/write.c but those routines don't follow our rules
			 * (no stdout/stderr IO) so we streamline it here.
			 */
			fn = buf->mesg.db_ipcs.fn;
			fn_len = buf->mesg.db_ipcs.fn_len;
			if ((YDB_PATH_MAX < fn_len) || ('\0' != *(fn + fn_len))
				|| (fn_len != (msglen - GTM_MESG_HDR_SIZE - offsetof(ipcs_mesg, fn[0]) - 1)))
			{
				buf->code = EINVAL;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(12) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 25"), process_id, buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
					RTS_ERROR_LITERAL("invalid file name argument"));
				break;
			}
			/* First open and read-in the fileheader */
			flags = O_RDWR;
			if (buf->mesg.db_ipcs.open_fd_with_o_direct)
			{
				fd_opened_with_o_direct = TRUE;
				flags = flags | O_DIRECT_FLAGS;
			} else
				fd_opened_with_o_direct = FALSE;
			OPENFILE(fn, flags, fd); /* udi not available so OPENFILE_DB not used */
			if (FD_INVALID == fd)
			{
				save_errno = buf->code = errno;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 26"), process_id, buf->pid, save_code,
					buf->mesg.id, ERR_DBFILOPERR, 2, fn_len, fn, save_errno);
				break;
			}
#			ifdef __MVS__
			if (-1 == gtm_zos_tag_to_policy(fd, TAG_BINARY, &realfiletag))
				TAG_POLICY_SEND_MSG(fn, errno, realfiletag, TAG_BINARY);
#			endif
			/* Verify is a GT.M database - validations:
			 *
			 * 1. Is a regular file (not a special type).
			 * 2. Is of a size at least as large as the file header (doesn't get fancy with this)
			 * 3. Able to read the file.
			 * 4. Verify tag at top of file denotes a GT.M database of the correct version.
			 * 5. Validate fields being set into database file (see details below).
			 * 6. Set the fields into the database.
			 * 7. Write updated database header and close the database.
			 */
			FSTAT_FILE(fd, &statbuf, save_errno);
			if (-1 == save_errno)
			{
				buf->code = save_errno;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 27"), process_id, buf->pid,
					save_code, buf->mesg.id, ERR_DBFILOPERR, 2, LEN_AND_STR(fn), save_errno);
				CLOSEFILE_RESET(fd, save_errno);	/* resets "fd" to FD_INVALID */
				break;
			}
			if (!S_ISREG(statbuf.st_mode) || (SIZEOF(header) > statbuf.st_size))
			{
				buf->code = ERR_DBNOTGDS;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(12) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 28"), process_id, buf->pid,
					save_code, buf->mesg.id, ERR_DBNOTGDS, 2, LEN_AND_STR(fn));
				CLOSEFILE_RESET(fd, save_errno);	/* resets "fd" to FD_INVALID */
				break;
			}
			if (fd_opened_with_o_direct)
			{
				fsb_size = get_fs_block_size(fd);
				DIO_BUFF_EXPAND_IF_NEEDED_NO_UDI(SGMNT_HDR_LEN, fsb_size, &(TREF(dio_buff)));
				csd = (sgmnt_data *)(TREF(dio_buff)).aligned;
			} else
				csd = &header;
			DB_LSEEKREAD(((unix_db_info *)NULL), fd, 0, csd, SGMNT_HDR_LEN, save_errno);
			if (0 != save_errno)
			{
				buf->code = save_errno;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 29"), process_id, buf->pid,
					save_code, buf->mesg.id, ERR_DBFILOPERR, 2, LEN_AND_STR(fn), save_errno);
				CLOSEFILE_RESET(fd, save_errno);	/* resets "fd" to FD_INVALID */
				break;
			}
			if (0 != memcmp(csd->label, GDS_LABEL, GDS_LABEL_SZ - 1))	/* Verify is GT.M database file */
			{
				buf->code = ERR_DBNOTGDS;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(12) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 30"), process_id, buf->pid,
					save_code, buf->mesg.id, ERR_DBNOTGDS, 2, LEN_AND_STR(fn));
				CLOSEFILE_RESET(fd, save_errno);	/* resets "fd" to FD_INVALID */
				break;
			}
			/* It would be easier to use the CHECK_DB_ENDIAN macro here but we'd prefer it didn't raise rts_error */
			check_endian.word32 = csd->minor_dbver;
			if (!check_endian.shorts.ENDIANCHECKTHIS)
			{
				buf->code = ERR_DBENDIAN;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 31"), process_id, buf->pid,
					save_code, buf->mesg.id, ERR_DBENDIAN, 4, fn_len, fn, ENDIANOTHER, ENDIANTHIS);
				CLOSEFILE_RESET(fd, save_errno);
				break;
			}
			/* Verify the fields to update make sense.
			 *
			 * 1. If new semid is INVALID_SEMID, then (file close):
			 *    a. New shmid should be INVALID_SHMID.
			 *    b. Both ctime fields should be zero.
			 *    c. If current value of semid in header is INVALID_SEMID, verify the other settings are also correct
			 *       for a closed database, close file.
			 *    d. If current value of semid is not INVALID_SEMID, check its value - should be <= 1 else error.
			 *    e. The current shmid should exist and have 1 attachment.
			 * 2. If new semid is *not* INVALID_SEMID then (file open):
			 *    a. Initial impulse is to check that new shmid is NOT INVALID_SHMID, but in one case which we can't
			 *       detect (standalone access of R/O DB by MUPIP INTEG and perhaps others), only the semaphore id
			 *       and ctime are modified. The shm fields are left as is.
			 *    b. If previous semid is not INVALID_SEMID, verify is same as we've been requested to update and close
			 *       with an already open message.
			 *    c. If previous semid is INVALID_SEMID, check the new semid and query its value. Should be <= 1 or
			 *       raise error.
			 *    d. Ditto for shmid and its attach value should also be <= 1.
			 *
			 * Note - Items 1c - 1e and items 2b - 2d not yet implemented (phase 3).
			 */
			if (INVALID_SEMID == buf->mesg.db_ipcs.semid)
			{	/* Intent is to close the given database */
				intent = intent_close;
				if ((INVALID_SHMID != buf->mesg.db_ipcs.shmid) || (0 != buf->mesg.db_ipcs.gt_sem_ctime)
				    || (0 != buf->mesg.db_ipcs.gt_shm_ctime))
				{
					buf->code = EINVAL;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(12) ERR_GTMSECSHRSRVFID, 6,
						RTS_ERROR_LITERAL("Server 32"), process_id,
						buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
						RTS_ERROR_LITERAL("Invalid header value combination"));
					CLOSEFILE_RESET(fd, save_errno);
					break;
				}
			} else
			{	/* Intent is to open the given database - Note if this is a standalone access of a read-only
				 * DB by such as MUPIP INTEG, only the semaphore is set, not shared memory so we must relax our
				 * checks a bit and allow the shm id and ctime to remain in "closed" state. But if shmid is
				 * specified, do validate that ctime was specified and vice-versa.
				 */
				intent = intent_open;
				if ((0 == buf->mesg.db_ipcs.gt_sem_ctime)
				    || ((INVALID_SEMID == buf->mesg.db_ipcs.shmid) && (0 != buf->mesg.db_ipcs.gt_shm_ctime))
				    || ((INVALID_SEMID != buf->mesg.db_ipcs.shmid) && (0 == buf->mesg.db_ipcs.gt_shm_ctime)))
				{
					buf->code = EINVAL;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(12) ERR_GTMSECSHRSRVFID, 6,
						RTS_ERROR_LITERAL("Server 33"), process_id,
						buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
						RTS_ERROR_LITERAL("Invalid header value combination"));
					CLOSEFILE_RESET(fd, save_errno);
					break;
				}
			}
			/* Update file header fields */
			csd->semid = buf->mesg.db_ipcs.semid;
			csd->shmid = buf->mesg.db_ipcs.shmid;
			csd->gt_sem_ctime.ctime = buf->mesg.db_ipcs.gt_sem_ctime;
			csd->gt_shm_ctime.ctime = buf->mesg.db_ipcs.gt_shm_ctime;
			/* And flush the changes back out. */
			GTMSECSHR_DB_LSEEKWRITE(((unix_db_info *)NULL), fd, 0, csd, SGMNT_HDR_LEN, save_errno);
			if (0 != save_errno)
			{
				buf->code = save_errno;
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
					RTS_ERROR_LITERAL("Server 34"), process_id,
					buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
					RTS_ERROR_LITERAL("Unable to write database file header"), save_errno);
				CLOSEFILE_RESET(fd, save_errno);
				break;
			}
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_GTMSECSHRUPDDBHDR, 5,
				buf->pid, fn_len, fn, RTS_ERROR_STRING(intent));
			buf->code = 0;
			CLOSEFILE_RESET(fd, save_errno);
			break;
		default:
			send_msg_csa(CSA_ARG(NULL) VARLSTCNT(12) ERR_GTMSECSHRSRVFID, 6,
				RTS_ERROR_LITERAL("Server 35"), process_id, buf->pid,
				buf->code, buf->mesg.id, ERR_TEXT, 2,
				RTS_ERROR_LITERAL("Invalid Service Request"));
			buf->code = 0x8000;	/* Flag for no-ack required - invalid commands get no response */
	}
	return;
}

/* Service request asks gtmsecshr to send a signal to a given process. Verify (as best we can) this process is running something
 * related to GT.M. Two potential methods are used:
 *
 * 1. Target process has an execution directory the same as ours ($ydb_dist). Note it is possible a target process is doing
 *    a call-in so this test is not always TRUE but is the faster of the two tests.
 * 2. Target process has libyottadb.{suffix} (aka YOTTADB_IMAGE_NAME from mdefsa.h) loaded which we can tell by examining the
 *    open files of the target process.
 *
 * Note - this routine is currently NOT USED as it is incomplete and not yet implemented for all platforms. We leave it in here
 * for now and plan to complete it in an upcoming version.
 */
int validate_receiver(gtmsecshr_mesg *buf, char *rundir, int rundir_len, int save_code)
{
	int	save_errno;
#	ifdef __linux__
#	  define PROCPATH_PREFIX	"/proc/"
#	  define PROCPATH_CMDLSUFFIX	"/cmdline"
#	  define PROCPATH_MAPSSUFFIX	"/maps"
	int	lnln, clrv, cmdbufln;
	FILE	*procstrm;
	char	procpath[YDB_PATH_MAX],	cmdbuf[YDB_PATH_MAX], rpcmdbuf[YDB_PATH_MAX];
	char	*ppptr, *ppptr_save, *csrv, *cptr;

	/* Check #1 - open /proc/<pid>/cmdline, read to first NULL - this is the command name */
	ppptr = procpath;
	MEMCPY_LIT(procpath, PROCPATH_PREFIX);
	ppptr += STRLEN(PROCPATH_PREFIX);
	ppptr = (char*)i2asc((uchar_ptr_t)ppptr, buf->mesg.id);
	ppptr_save = ppptr;			/* Save where adding cmdline so can replace if need to move to check #2 */
	memcpy(ppptr, PROCPATH_CMDLSUFFIX, SIZEOF(PROCPATH_CMDLSUFFIX));	/* Copy includes terminating null of literal */
	Fopen(procstrm, procpath, "r");
	if (NULL == procstrm)
	{
		save_errno = errno;
		buf->code = save_errno;
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
			RTS_ERROR_LITERAL("Server 36"), process_id, buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
			RTS_ERROR_LITERAL("Could not open /proc/<pid>/cmdline"), save_errno);
		return save_errno;
	}
	FGETS(cmdbuf, YDB_PATH_MAX, procstrm, csrv);
	if (NULL == csrv)
	{
		save_errno = errno;
		buf->code = save_errno;
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
			RTS_ERROR_LITERAL("Server 37"), process_id, buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
			RTS_ERROR_LITERAL("Could not read /proc/<pid>/cmdline"), save_errno);
	}
	FCLOSE(procstrm, clrv);
	if (-1 == clrv)
		/* Not a functional issue so just warn about it in op-log */
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) MAKE_MSG_WARNING(ERR_SYSCALL), 5, LEN_AND_LIT("fclose()"),
			     CALLFROM, errno);
	if (NULL == csrv)
		return save_errno;
	lnln = STRLEN(cmdbuf);
	/* Look from the end backwards to find the last '/' to isolate the directory */
	for (cptr = cmdbuf + lnln - 1; (cptr >= cmdbuf) && ('/' != *cptr); cptr--)
		;
	cmdbufln = INTCAST(cptr - cmdbuf);
	if (0 < cmdbufln)
	{	/* Normalize the directory via realpath so comparison possible */
		cmdbuf[cmdbufln] = rpcmdbuf[0] = '\0';
		csrv = realpath(cmdbuf, rpcmdbuf);
		cmdbufln = STRLEN(rpcmdbuf);
		if ((cmdbufln == rundir_len) && (0 == memcmp(rundir, rpcmdbuf, cmdbufln)))
		{
			buf->code = 0;		/* Successful validation */
			DEBUG_ONLY(util_out_print("Successful validation of target processid !UL", OPER, buf->pid));
			return 0;
		}
	}
	/* Check #1 failed - attempt check #2 - read /proc/<pid>/maps to see if libyottadb is there */
	memcpy(ppptr_save, PROCPATH_MAPSSUFFIX, SIZEOF(PROCPATH_MAPSSUFFIX));	/* Copy includes terminating null of literal */
	Fopen(procstrm, procpath, "r");
	if (NULL == procstrm)
	{
		save_errno = errno;
		buf->code = save_errno;
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(13) ERR_GTMSECSHRSRVFID, 6,
			RTS_ERROR_LITERAL("Server 38"), process_id, buf->pid, save_code, buf->mesg.id, ERR_TEXT, 2,
			RTS_ERROR_LITERAL("Could not open /proc/<pid>/cmdline"), save_errno);
		return save_errno;
	}
	/* Insert map reading code TODO */
	FCLOSE(procstrm, clrv);
	if (-1 == clrv)
		/* Not a functional issue so just warn about it in op-log */
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) MAKE_MSG_WARNING(ERR_SYSCALL), 5, LEN_AND_LIT("fclose()"), CALLFROM, errno);
#	endif
	return 0;
}

#ifdef __MVS__
boolean_t gtm_tag_error(char *filename, int realtag, int desiredtag)
{
	char *errmsg;

	errmsg = STRERROR(errno);
	send_msg_csa(CSA_ARG(NULL) VARLSTCNT(10) ERR_BADTAG, 4, LEN_AND_STR(filename),
		realtag, desiredtag, ERR_TEXT, 2, RTS_ERROR_STRING(errmsg));
	return FALSE;
}
#endif
