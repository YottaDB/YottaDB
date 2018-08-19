/****************************************************************
 *								*
 * Copyright (c) 2008-2018 Fidelity National Information	*
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

/********************************************************************************************
 * W A R N I N G  ---  W A R N I N G  --- W A R N I N G --- W A R N I N G --- W A R N I N G *
 *											    *
 * This routine (gtmsecshr_wrapper) runs as setuid to root to perform "environmental        *
 * cleanup" prior to invoking gtmsecshr proper. Extreme care must be taken to prevent all   *
 * forms of deceptive access,  linking with unauthorized libraries, etc. Same applies to    *
 * anything it calls.		    							    *
 *											    *
 * W A R N I N G  ---  W A R N I N G  --- W A R N I N G --- W A R N I N G --- W A R N I N G *
 ********************************************************************************************/


#include "mdef.h"
#define BYPASS_MEMCPY_OVERRIDE  /* Signals gtm_string.h to not override memcpy(). This causes linking problems when libmumps.a
                                 * is not available.
                                 */
/* We want system malloc, not gtm_malloc (which comes from mdef.h --> mdefsp.h).  Since gtmsecshr_wrapper runs as root,
 * using the system malloc will increase security over using gtm_malloc.  Additionally, by not using gtm_malloc, we
 * are reducing code bloat.
 */
#undef malloc
#undef free
#include "gtm_unistd.h"
#include "gtm_stat.h"
#include "gtm_stdlib.h"
#include "gtm_stdio.h"
#include "gtm_string.h"
#include "gtm_syslog.h"
#include "main_pragma.h"
#include "gtm_signal.h"
#if !defined(__MVS__) && !defined(__APPLE__)
#  include <malloc.h>
#endif
#include <errno.h>

#define WBOX_GBLDEF		/* Causes whitebox global vars to be GBLDEF'd instead of GBLREF'd thus keeping gbldefs.c out
				 * of gtmsecshr_wrapper the executable.
				 */
#include "wbox_test_init.h"
#include "gtm_limits.h"
#include "get_syslog_flags.h"
#include "ydb_getenv.h"

#define ROOTUID 0
#define ROOTGID 0
#define MAX_ENV_VAR_VAL_LEN 1024
#define MAX_ALLOWABLE_LEN 256
#define ASCIICTLMAX	32  /* space character */
#define ASCIICTLMIN	0   /* NULL character */
#define YDB_TMP		"ydb_tmp"
#define	SUB_PATH_TO_GTMSECSHRDIR "/gtmsecshrdir"
#define	REL_PATH_TO_CURDIR "."
#define	REL_PATH_TO_GTMSECSHR "./gtmsecshr"
#define	GTMSECSHR_BASENAME "/gtmsecshr"
#define MAX_ENV_NAME_LEN 2048

/* Build up some defines for use with AIX and reading /etc/environment to pick up a default TZ value plus some defines
 * for testing AIX errors in this wrapper with white box test cases.
 */
#ifdef _AIX
#define GTM_TZ				"TZ"
#define TZLOCATOR			"TZ="
#define NEWLINE				0x0a
#define GTMETCDIRPATH			"/etc"
#define BADGTMETCDIRPATH		"/bogusdirnotetc"
#define GTMENVIRONFILE			"environment"
#define BADGTMENVIRONFILE		"bogusfilenotenvironment"
#endif

typedef	char	**environptr_t;
extern	char	**environ;

/* Since gtmsecshr_wrapper.c is a stand-alone module, we cannot use error_def-style definitions, so use simple macro defines to
 * initialize all error types used along with their respective mnemonics. Note that we need two '%' to ensure the '%' prefix
 * in the syslog.
 */
#define ERR_SECSHRCLEARENVFAILED		\
	"%%YDB-E-SECSHRCLEARENVFAILED, clearenv failed. gtmsecshr will not be started\n"
#define ERR_SECSHRCHDIRFAILED1			\
	"%%YDB-E-SECSHRCHDIRFAILED1, chdir failed on %s, errno %d. gtmsecshr will not be started\n"
#define ERR_SECSHRCHDIRFAILED2			\
	"%%YDB-W-SECSHRCHDIRFAILED2, chdir failed on %s, errno %d. gtmsecshr will be started with GMT timezone\n"
#define ERR_SECSHREXECLFAILED			\
	"%%YDB-E-SECSHREXECLFAILED, execl of %s failed\n"
#define ERR_SECSHRYDBDIST2LONG			\
	"%%YDB-E-SECSHRYDBDIST2LONG, ydb_dist env var too long. gtmsecshr will not be started\n"
#define ERR_SECSHRYDBTMP2LONG			\
	"%%YDB-E-SECSHRYDBTMP2LONG, ydb_tmp/gtm_tmp env var too long. gtmsecshr will not be started\n"
#define ERR_SECSHRNOYDBDIST			\
	"%%YDB-E-SECSHRNOYDBDIST, ydb_dist env var does not exist. gtmsecshr will not be started\n"
#define ERR_SECSHRNOTOWNEDBYROOT		\
	"%%YDB-E-SECSHRNOTOWNEDBYROOT, %s not owned by root. gtmsecshr will not be started\n"
#define ERR_SECSHRNOTSETUID			\
	"%%YDB-E-SECSHRNOTSETUID, %s not set-uid. gtmsecshr will not be started\n"
#define ERR_SECSHRPERMINCRCT			\
	"%%YDB-E-SECSHRPERMINCRCT, %s permissions incorrect (%04o). gtmsecshr will not be started\n"
#define ERR_SECSHRSETYDBDISTFAILED		\
	"%%YDB-E-SECSHRSETYDBDISTFAILED, setenv for ydb_dist failed. gtmsecshr will not be started\n"
#define ERR_SECSHRSETYDBTMPFAILED		\
	"%%YDB-E-SECSHRSETYDBTMPFAILED, setenv for ydb_tmp failed. gtmsecshr will not be started\n"
#define ERR_SECSHRSETUIDFAILED			\
	"%%YDB-E-SECSHRSETUIDFAILED, setuid failed. gtmsecshr will not be started\n"
#define ERR_SECSHRSTATFAILED			\
	"%%YDB-E-SECSHRSTATFAILED, stat failed on %s, errno %d. gtmsecshr will not be started\n"
#define ERR_SECSHRTZFAIL			\
	"%%YDB-W-SECSHRTZFAIL, %s %d. gtmsecshr will start with TZ set to GMT\n"
#define ERR_SECSHRWRITABLE			\
	"%%YDB-E-SECSHRWRITABLE, %s writable. gtmsecshr will not be started\n"

/*
Make sure these are synced with the above. We need this comment for the InfoHub tools to generate message
information for gtmsecshr_wrapper.c, since it does not have a stand-alone .msg file.

	.FACILITY	GTMSECSHRINIT,251/PREFIX=ERR_

SECSHRCHDIRFAILED1	<chdir failed on !AD, errno !UL. gtmsecshr will not be started>/error/fao=3!/ansi=0
SECSHRCHDIRFAILED2	<chdir failed on !AD, errno !UL. gtmsecshr will be started with GMT timezone>/warning/fao=3!/ansi=0
SECSHRCLEARENVFAILED	<clearenv failed. gtmsecshr will not be started>/error/fao=0!/ansi=0
SECSHREXECLFAILED	<execl of !AD failed>/error/fao=2!/ansi=0
SECSHRYDBDIST2LONG	<ydb_dist env var too long. gtmsecshr will not be started>/error/fao=0!/ansi=0
SECSHRYDBTMP2LONG	<ydb_tmp/gtm_tmp env var too long. gtmsecshr will not be started>/error/fao=0!/ansi=0
SECSHRNOYDBDIST		<ydb_dist env var does not exist. gtmsecshr will not be started>/error/fao=0!/ansi=0
SECSHRNOTOWNEDBYROOT	<!AD not owned by root. gtmsecshr will not be started>/error/fao=3!/ansi=0
SECSHRNOTSETUID		<!AD not set-uid. gtmsecshr will not be started>/error/fao=2!/ansi=0
SECSHRPERMINCRCT	<!AD permissions incorrect (0!UL). gtmsecshr will not be started>/error/fao=3!/ansi=0
SECSHRSETYDBDISTFAILED	<setenv for ydb_dist failed. gtmsecshr will not be started>/error/fao=0!/ansi=0
SECSHRSETYDBTMPFAILED	<setenv for ydb_tmp failed. gtmsecshr will not be started>/error/fao=0!/ansi=0
SECSHRSETUIDFAILED	<setuid failed. gtmsecshr will not be started>/error/fao=0!/ansi=0
SECSHRSTATFAILED	<stat failed on !AD, errno !UL. gtmsecshr will not be started>/error/fao=3!/ansi=0
SECSHRTZFAIL		<!AD !UL. gtmsecshr will start with TZ set to GMT>/warning/fao=3!/ansi=0
SECSHRWRITABLE		<!AD writable. gtmsecshr will not be started>/error/fao=2!/ansi=0
! the following line stops getmsginfo.m
	.end
*/

void strsanitize(char *src, char *dst);
void strsanitize(char *src, char *dst)
{
	int i, srclen;

	/* The calling function already validates the string length. */
	srclen = strlen(src);
	for (i = 0; (i <= srclen) && (MAX_ENV_VAR_VAL_LEN > i); i++)
	{
		/* Convert all control characters to '*'. */
		if ((ASCIICTLMAX > (int)src[i]) && (ASCIICTLMIN < (int)src[i]))
			dst[i] = '*';
		else
			dst[i] = src[i];
	}
}

int main()
{
	int		ret, status;
	char 		*env_var_ptr;
	struct stat	gtm_secshrdir_stat;
	struct stat	gtm_secshr_stat;
	char 		ydb_dist_val[MAX_ENV_VAR_VAL_LEN];
	char 		ydb_tmp_val[MAX_ENV_VAR_VAL_LEN];
	char 		gtm_secshrdir_path[MAX_ENV_VAR_VAL_LEN];
	char 		gtm_secshrdir_path_display[MAX_ENV_VAR_VAL_LEN];
	char 		gtm_secshr_path[MAX_ENV_VAR_VAL_LEN];
	char 		gtm_secshr_path_display[MAX_ENV_VAR_VAL_LEN];
	char 		gtm_secshr_orig_path[MAX_ENV_VAR_VAL_LEN];
	boolean_t	ydb_tmp_exists = FALSE;
	int		rc;
	sigset_t	mask;
#	ifdef _AIX
	FILE		*envfile;
	int		recnum, reclen, save_errno;
	char		*fgets_rc;
	boolean_t	gtm_TZ_found;
	char		gtm_TZ_val[MAX_ENV_VAR_VAL_LEN + 1], *gtm_TZ_val_ptr;
	char		*etcdirpath, *environfile;
#	endif

	/* Reset the signal mask (since the one inherited from the invoking process might have signals such as SIGALRM or SIGTERM
	 * blocked) to let gtmsecshr manage its own signals using sig_init.
	 */
	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);	/* BYPASSOK(sigprocmask) */
	OPENLOG("GTMSECSHRINIT", get_syslog_flags(), LOG_USER);
#	ifdef _AIX
#	ifdef DEBUG
	/* Use some very simplistic processing to obtain values for $ydb_white_box_test_case_enable/number since we are basically
	 * standalone in this routine without the ability to call into other mumps routines. We fetch the value and convert it
	 * numerically as best as possible. For the boolean enable flag, If it's non-zero - it's true else false. No errors raised
	 * here for conversions.
	 */
	env_var_ptr = ydb_getenv(YDBENVINDX_WHITE_BOX_TEST_CASE_ENABLE, NULL_SUFFIX, NULL_IS_YDB_ENV_MATCH);
	if (NULL != env_var_ptr)
	{
		ydb_white_box_test_case_enabled = atoi(env_var_ptr);
		if (ydb_white_box_test_case_enabled)
		{
			env_var_ptr = ydb_getenv(YDBENVINDX_WHITE_BOX_TEST_CASE_NUMBER, NULL_SUFFIX, NULL_IS_YDB_ENV_MATCH);
			if (NULL != env_var_ptr)
				ydb_white_box_test_case_number = atoi(env_var_ptr);
		}
	}
#	endif /* DEBUG */
	etcdirpath = !(WBTEST_ENABLED(WBTEST_SECSHRWRAP_NOETC)) ? GTMETCDIRPATH : BADGTMETCDIRPATH;
	environfile = !(WBTEST_ENABLED(WBTEST_SECSHRWRAP_NOENVIRON)) ? GTMENVIRONFILE : BADGTMENVIRONFILE;
	/* Note syslog timestamps are handled somewhat differently on AIX. If one undefines the TZ environment variable (such as
	 * our wrapper does to prevent reporting time in the timezone of the process that started gtmsecshr, process time reverts
	 * to GMT. So to prevent that, lookup the default timezone in /etc/environment and use it to prevent whacky time reporting
	 * by gtmsecshr in the operator log. Secure file read method is to switch dir first, then do relative open.
	 */
	gtm_TZ_found = FALSE;
	if (-1 == CHDIR(etcdirpath))	/* Note chdir is changed again below so this is only temporary */
		SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRCHDIRFAILED2, etcdirpath, errno);
	else
	{
		envfile = fopen(environfile, "r");
		if (NULL == envfile)
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRTZFAIL, "Unable to open /etc/environment errno:", errno);
		else
		{	/* /etc/environments is open, locate TZ= record */
			for (recnum = 0; ; recnum++)
			{
				FGETS(gtm_TZ_val, MAX_ENV_VAR_VAL_LEN, envfile, fgets_rc);
				if (NULL == fgets_rc)
					break;
				if (STRLEN(TZLOCATOR) >= (reclen = STRLEN(gtm_TZ_val)))		/* Note assignment */
					continue;
				if (0 == memcmp(TZLOCATOR, gtm_TZ_val, STRLEN(TZLOCATOR)))
				{
					gtm_TZ_found = TRUE;
					break;
				}
			}
			if (!gtm_TZ_found DEBUG_ONLY(|| WBTEST_ENABLED(WBTEST_SECSHRWRAP_NOTZREC_READERR)
						     || WBTEST_ENABLED(WBTEST_SECSHRWRAP_NOTZREC_EOF)))
			{	/* We didn't find the TZ record - report it depending if we just hit EOF or something more
				 * severe.
				 */
				save_errno = errno;
#				if DEBUG
				/* Since we may have come here via the white box tests, let's ensure gtm_TZ_found is FALSE */
				gtm_TZ_found = FALSE;
				/* Separately test for some white box conditions before testing the real return codes */
				if (WBTEST_ENABLED(WBTEST_SECSHRWRAP_NOTZREC_READERR))
					SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRTZFAIL, "FGETS() failure reading /etc/environment "
                                               "errno:", 999);
				else if (WBTEST_ENABLED(WBTEST_SECSHRWRAP_NOTZREC_EOF))
					SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRTZFAIL, "File /etc/environment has no default TZ ",
					       0);	/* This puts a spurious 0 out with the message but this condition should
							 * be so rare as to be irrelevant and not worth a separate message.
							 */
				else
#				endif
				if (!feof(envfile))
					SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRTZFAIL, "FGETS() failure reading /etc/environment "
					       "errno:", save_errno);
				else
					/* Have EOF - didn't find TZ */
					SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRTZFAIL, "File /etc/environment has no default TZ ",
					       0);	/* This puts a spurious 0 out with the message but this condition should
							 * be so rare as to be irrelevant and not worth a separate message.
							 */
			} else
			{	/* TZ record acquired - isolate TZ value. Note we don't allocate storage for it like we would if
				 * this were a long(er) running process but this process's sole job is to call the real gtmsecshr
				 * so the stack storage flavor of the buffer is fine.
				 */
				if (NEWLINE == gtm_TZ_val[reclen - 1])
					gtm_TZ_val[--reclen] = '\0';		/* Overwrite nl with null and decr length */
				/* Set pointer to point past the TZ= part of the value pair as we use setenv() to set this value */
				gtm_TZ_val_ptr = gtm_TZ_val + STRLEN(TZLOCATOR);
				/* In case this default TZ is different from the process that started gtmsecshr, go ahead and
				 * establish this version now for any remaining messages that happen before we do a clearenv().
				 * We will re-establish the value again after the clear. Until we establish the default value
				 * here, gtmsecshr wrapper messages will have the timezone of the invoking process.
				 */
				status = setenv(GTM_TZ, gtm_TZ_val_ptr, TRUE);
				if ((0 != status) || WBTEST_ENABLED(WBTEST_SECSHRWRAP_SETENVFAIL1))
				{
					save_errno = errno;
#					ifdef DEBUG
					if (WBTEST_ENABLED(WBTEST_SECSHRWRAP_SETENVFAIL1))
					    save_errno = 999;
#					endif
					SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRTZFAIL, "TZ reset with setenv() failed (1) errno:",
					       save_errno);
				}
			}
			fclose(envfile);
		}
	}
#	endif /* _AIX */
	ret = 0; /* start positive */
	/* get the ones we need */
	if (env_var_ptr = ydb_getenv(YDBENVINDX_DIST_ONLY, NULL_SUFFIX, NULL_IS_YDB_ENV_MATCH))	/* Warning - assignment */
	{
		if (MAX_ALLOWABLE_LEN < (strlen(env_var_ptr) + STR_LIT_LEN(SUB_PATH_TO_GTMSECSHRDIR)
		    + STR_LIT_LEN(GTMSECSHR_BASENAME)))
		{
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRYDBDIST2LONG);
			ret = -1;
		} else
		{
			strcpy(ydb_dist_val, env_var_ptr);
			/* point the path to the real gtmsecshr - for display purposes only */
			strcpy(gtm_secshr_path, env_var_ptr);
			strcat(gtm_secshr_path, SUB_PATH_TO_GTMSECSHRDIR);
			strcat(gtm_secshr_path, GTMSECSHR_BASENAME);
			strsanitize(gtm_secshr_path, gtm_secshr_path_display);
			/* point the path to the real gtmsecshrdir */
			strcpy(gtm_secshrdir_path, env_var_ptr);
			strcat(gtm_secshrdir_path, SUB_PATH_TO_GTMSECSHRDIR);
			strsanitize(gtm_secshrdir_path, gtm_secshrdir_path_display);
		}
	} else
	{
		SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRNOYDBDIST);
		ret = -1;
	}
	if (env_var_ptr = ydb_getenv(YDBENVINDX_TMP, NULL_SUFFIX, NULL_IS_YDB_ENV_MATCH))	/* Warning - assignment */
	{
		if (MAX_ALLOWABLE_LEN < strlen(env_var_ptr))
		{
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRYDBTMP2LONG);
			ret = -1;
		} else
		{
			ydb_tmp_exists = TRUE;
			strcpy(ydb_tmp_val, env_var_ptr);
		}
	}
	if (!ret)
	{	/* clear all */
#		if defined(SUNOS) || defined (__CYGWIN__) || defined (__APPLE__) /* Fixed by OSE/SMH - environ = NULL causes failures later*/
		char *clearenv = NULL;
		environ = &clearenv;
          	status = 0;
#		else
		status = clearenv();
#		endif
		if (status)
		{
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRCLEARENVFAILED);
			ret = -1;
		}
		/* add the ones we need */
		status = setenv(ydbenvname[YDBENVINDX_DIST] + 1, ydb_dist_val, TRUE);	/* + 1 to skip leading $ */
		if (status)
		{
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRSETYDBDISTFAILED);
			ret = -1;
		}
		if (ydb_tmp_exists)
		{
			status = setenv(YDB_TMP, ydb_tmp_val, TRUE);
			if (status)
			{
				SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRSETYDBTMPFAILED);
				ret = -1;
			}
		}
#		ifdef _AIX
		if (gtm_TZ_found DEBUG_ONLY(|| WBTEST_ENABLED(WBTEST_SECSHRWRAP_SETENVFAIL2)))
		{
#			ifdef DEBUG
			if (gtm_TZ_found)	/* Want to run setenv even if WBOX if TZ was found so we can find log entries */
				status = setenv(GTM_TZ, gtm_TZ_val_ptr, TRUE);
			if (WBTEST_ENABLED(WBTEST_SECSHRWRAP_SETENVFAIL2))
				status = -1;
#			else
			status = setenv(GTM_TZ, gtm_TZ_val_ptr, TRUE);
#			endif
			if (0 != status)
			{
				save_errno = errno;
#				ifdef DEBUG
				if (WBTEST_ENABLED(WBTEST_SECSHRWRAP_SETENVFAIL2))
					save_errno = 999;
#				endif
				SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRTZFAIL, "TZ reset with setenv() failed (2) errno:",
				       save_errno);
			}
		}
#		endif
	}
	if (!ret)
	{	/* go to root */
		if (-1 == CHDIR(gtm_secshrdir_path))
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRCHDIRFAILED1, gtm_secshrdir_path_display, errno);
		else if (-1 == Stat(REL_PATH_TO_CURDIR, &gtm_secshrdir_stat))
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRSTATFAILED, gtm_secshrdir_path_display, errno);
		else if (ROOTUID != gtm_secshrdir_stat.st_uid)
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRNOTOWNEDBYROOT, gtm_secshrdir_path_display);
		else if (gtm_secshrdir_stat.st_mode & 0277)
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRPERMINCRCT, gtm_secshrdir_path_display,
					gtm_secshrdir_stat.st_mode & 0777);
		else if (-1 == Stat(REL_PATH_TO_GTMSECSHR, &gtm_secshr_stat))
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRSTATFAILED, gtm_secshr_path_display, errno);
		else if (ROOTUID != gtm_secshr_stat.st_uid)
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRNOTOWNEDBYROOT, gtm_secshr_path_display);
		else if (gtm_secshr_stat.st_mode & 022)
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRWRITABLE, gtm_secshr_path_display);
		else if (!(gtm_secshr_stat.st_mode & 04000))
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRNOTSETUID, gtm_secshr_path_display);
		else if (-1 == setuid(ROOTUID))
			SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHRSETUIDFAILED);
		else
		{	/* call the real gtmsecshr, but have ps display the original gtmsecshr location */
			strcpy(gtm_secshr_orig_path, ydb_dist_val);
			strcat(gtm_secshr_orig_path, GTMSECSHR_BASENAME);
			ret = execl(REL_PATH_TO_GTMSECSHR, gtm_secshr_orig_path, NULL);
			if (-1 == ret)
				SYSLOG(LOG_USER | LOG_INFO, ERR_SECSHREXECLFAILED, gtm_secshr_path_display);
		}
	}
	CLOSELOG();
	return ret;
}
