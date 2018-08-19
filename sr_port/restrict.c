/****************************************************************
 *								*
 * Copyright (c) 2017-2018 Fidelity National Information	*
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

#include <errno.h>
#include <grp.h>

#include "mdef.h"
#include "gtmio.h"
#include "gtm_common_defs.h"
#include "gtm_stdio.h"
#include "gtm_string.h"
#include "gtm_limits.h"
#include "gtm_strings.h"
#include "gtm_malloc.h"
#include "gtm_permissions.h"
#include "send_msg.h"
#include "restrict.h"
#include "gtm_file_remove.h"
#include "gtm_time.h"

#define RESTRICT_FILENAME		"restrict.txt"
#define RESTRICT_BREAK			"BREAK"
#define RESTRICT_ZBREAK			"ZBREAK"
#define RESTRICT_ZEDIT			"ZEDIT"
#define RESTRICT_ZSYSTEM		"ZSYSTEM"
#define RESTRICT_PIPEOPEN		"PIPE_OPEN"
#define RESTRICT_TRIGMOD		"TRIGGER_MOD"
#define RESTRICT_CENABLE		"CENABLE"
#define RESTRICT_DSE			"DSE"
#define RESTRICT_DIRECT_MODE		"DIRECT_MODE"
#define RESTRICT_ZCMDLINE		"ZCMDLINE"
#define RESTRICT_HALT			"HALT"
#define RESTRICT_ZHALT			"ZHALT"
#define ZSYSTEM_FILTER			"ZSYSTEM_FILTER"
#define PIPE_FILTER			"PIPE_FILTER"
#define MAX_READ_SZ			1024	/* Restrict Mnemonic shouldn't exceed this limit */
#define MAX_FACILITY_LEN		64
#define MAX_GROUP_LEN			64
#define STRINGIFY(S)			#S
#define BOUNDED_FMT(LIMIT,TYPE)		"%" STRINGIFY(LIMIT) TYPE
#define FACILITY_FMTSTR			BOUNDED_FMT(MAX_FACILITY_LEN, "[A-Za-z0-9_]")
#define GROUP_FMTSTR			BOUNDED_FMT(MAX_GROUP_LEN, "[A-Za-z0-9_^%]")

GBLDEF	struct restrict_facilities	restrictions;
GBLDEF	boolean_t			restrict_initialized;

#ifdef DEBUG
GBLREF	boolean_t			ydb_dist_ok_to_use;
#endif
GBLREF	char				ydb_dist[YDB_PATH_MAX];

STATICFNDCL void append_filter(char *, char *, char *, int *, char *);

error_def(ERR_RESTRICTSYNTAX);
error_def(ERR_TEXT);

#if defined st_mtime
# ifdef __APPLE__
#   define st_mtim		st_mtimespec
# else
#   define st_mtim		st_mtim
# endif
#endif

#define	PUT_FLNAME_IN_MAPPING_FILE(RPATH, FPATH, C_CALL_NAME, M_REF_NAME, STAT_RM,					\
						CREATED_NOW, CREATED_NOW_INITIALIZED, SAVE_ERRNO, ERR_STR)		\
{															\
	if (CREATED_NOW_INITIALIZED)											\
	{														\
		/* Note: "CREATED_NOW" is initialized/usable only if "CREATED_NOW_INITIALIZED" is TRUE */		\
		if (CREATED_NOW)											\
		{	/* We created this file in a prior invocation of PUT_FLNAME_IN_MAPPING_FILE			\
			 * so append all future macro invocations into the same file.					\
			 */												\
			append_filter(FPATH, C_CALL_NAME, M_REF_NAME, SAVE_ERRNO, ERR_STR);				\
		}													\
		/* else : We have already determined restrict.txt and filter_commands.tab are in sync time wise.	\
		 *        No need to do any more file modification checks.						\
		 */													\
	} else if (ACCESS(FPATH, F_OK))											\
	{	/* File does not exist, create and write mapping */							\
		CREATED_NOW = TRUE;											\
		CREATED_NOW_INITIALIZED = TRUE;										\
		append_filter(FPATH, C_CALL_NAME, M_REF_NAME, SAVE_ERRNO, ERR_STR);					\
	} else														\
	{	/* Filter file exists. Check modified time */								\
		Stat(RPATH, &rTime);											\
		rmtime = rTime.st_mtim;											\
		Stat(FPATH, &fTime);											\
		fmtime = fTime.st_mtim;											\
		/* Check if restrict.txt file modification time (rmtime) is newer than					\
		 * filter_commands.tab file modification time (fmtime). If so, recreate filter_commands.tab.		\
		 */													\
		if ((rmtime.tv_sec > fmtime.tv_sec)									\
			|| ((rmtime.tv_sec == fmtime.tv_sec) && (rmtime.tv_nsec >= fmtime.tv_nsec)))			\
		{	/* Delete the older mapping file and recreate new if required */				\
			CREATED_NOW = TRUE;										\
			gtm_file_remove(FPATH, strlen(FPATH), &STAT_RM);						\
			append_filter(FPATH, C_CALL_NAME, M_REF_NAME, SAVE_ERRNO, ERR_STR);				\
		} else													\
			CREATED_NOW = FALSE;										\
		CREATED_NOW_INITIALIZED = TRUE;										\
	}														\
}

void append_filter(char * fpath, char * c_call_name, char * m_ref_name, int * save_errno, char * errstr)
{
	FILE	*fp;

	Fopen(fp, fpath, "a+");
	if (NULL != fp)
	{
		fprintf(fp, "%s : gtm_long_t* %s(I:gtm_char_t*, O:gtm_string_t*)\n",
							c_call_name, m_ref_name);
		fclose(fp);
	}
	else
	{
		*save_errno = errno;
		SNPRINTF(errstr, SIZEOF(errstr), "fopen() : %s", COMM_FILTER_FILENAME);
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5,
                                         LEN_AND_STR(errstr), CALLFROM, save_errno);
	}
}

void restrict_init(void)
{
	char		rfpath[YDB_PATH_MAX], rcfpath[YDB_PATH_MAX];
	char		linebuf[MAX_READ_SZ+1], *lbp, facility[MAX_FACILITY_LEN+1], group_or_flname[MAX_GROUP_LEN+1];
	char		errstr[MAX_FN_LEN + 1];
	int		save_errno, fields, status, lineno;
	uint4		statrm;
	FILE		*rfp;
	boolean_t	restrict_one, restrict_all = FALSE;
	struct group	grp, *grpres;
	char		*grpbuf = NULL;
	size_t		grpbufsz;
	boolean_t	created_now, created_now_initialized = FALSE;
	struct stat 	rTime, fTime;
	struct timespec	rmtime, fmtime;

	assert(!restrict_initialized);
	assert(ydb_dist_ok_to_use);
	SNPRINTF(rfpath, YDB_PATH_MAX, "%s/%s", ydb_dist, RESTRICT_FILENAME);
	SNPRINTF(rcfpath, YDB_PATH_MAX, "%s/%s", ydb_dist, COMM_FILTER_FILENAME);
	if (-1 == ACCESS(rfpath, W_OK))
	{	/* Write access implies no restrictions. Otherwise try reading the file for facilities to restrict. */
		save_errno = errno;
		if ((EACCES == save_errno) || (EROFS == save_errno))
		{	/* The file exists, but we don't have write permissions. Try to read it to determine restricted facilities.
			 * Other errors indicate that the file is unavailable for some reason not associated with permissions,
			 * e.g. missing, so no restrictions.
			 */
			Fopen(rfp, rfpath, "r");
			if (NULL == rfp)
			{	/* Normally, this could mean that the file didn't exist, but since ACCESS() gave us either
				 * EACCES or EROFS above, the file exists, and the current error means we have no read
				 * access. No read access means restrict everything.
				 */
				restrict_all = TRUE;
			} else
			{	/* Read the file, line by line. */
				lineno = 0;
				do
				{
					FGETS_FILE(linebuf, MAX_READ_SZ, rfp, lbp);
					if (NULL != lbp)
					{
						lineno++;
						fields = SSCANF(linebuf, FACILITY_FMTSTR " : " GROUP_FMTSTR,
											facility, group_or_flname);
						if (0 == fields)
							continue;	/* Ignore blank lines */
						else if (1 == fields)
							restrict_one = TRUE;
						else if (2 == fields)
						{
							if (NULL == grpbuf)
							{
								SYSCONF(_SC_GETGR_R_SIZE_MAX, grpbufsz);
								grpbuf = malloc(grpbufsz);
							}
							assert(NULL != grpbuf);
							if (0 == STRNCASECMP(facility, ZSYSTEM_FILTER, SIZEOF(ZSYSTEM_FILTER)))
							{
								restrictions.zsy_filter = TRUE;
								PUT_FLNAME_IN_MAPPING_FILE(rfpath, rcfpath,
									ZSY_C_CALL_NAME, group_or_flname, statrm,
									created_now, created_now_initialized,
									&save_errno, errstr);
								continue;
							}
							if (0 == STRNCASECMP(facility, PIPE_FILTER, SIZEOF(PIPE_FILTER)))
							{
								restrictions.pipe_filter = TRUE;
								PUT_FLNAME_IN_MAPPING_FILE(rfpath, rcfpath,
									PIPE_C_CALL_NAME, group_or_flname, statrm,
									created_now, created_now_initialized,
									&save_errno, errstr);
								continue;
							}
							status = getgrnam_r(group_or_flname, &grp, grpbuf, grpbufsz, &grpres);
							if (0 == status)
							{
								if (NULL == grpres)
								{	/* Treat error in group lookup as parse error. */
									send_msg_csa(CSA_ARG(NULL) VARLSTCNT(9)
											ERR_RESTRICTSYNTAX, 3,
											LEN_AND_STR(rfpath), lineno, ERR_TEXT, 2,
											LEN_AND_LIT("Unknown group"));
									restrict_all = TRUE;
									break;
								} else if ((GETEGID() == grp.gr_gid) || GID_IN_GID_LIST(grp.gr_gid))
									restrict_one = FALSE;
								else
									restrict_one = TRUE;
							} else
							{	/* Treat error in group lookup as parse error. */
								send_msg_csa(CSA_ARG(NULL) VARLSTCNT(12) ERR_RESTRICTSYNTAX, 3,
										LEN_AND_STR(rfpath), lineno, ERR_SYSCALL, 5,
										RTS_ERROR_LITERAL("getgrnam_r"), CALLFROM, status);
								restrict_all = TRUE;
								break;
							}
						} else
						{
							restrict_all = TRUE;	/* Parse error - restrict everything */
							break;
						}
						if (0 == STRNCASECMP(facility, RESTRICT_BREAK, SIZEOF(RESTRICT_BREAK)))
							restrictions.break_op = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_ZBREAK, SIZEOF(RESTRICT_ZBREAK)))
							restrictions.zbreak_op = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_ZEDIT, SIZEOF(RESTRICT_ZEDIT)))
							restrictions.zedit_op = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_ZSYSTEM, SIZEOF(RESTRICT_ZSYSTEM)))
							restrictions.zsystem_op = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_PIPEOPEN, SIZEOF(RESTRICT_PIPEOPEN)))
							restrictions.pipe_open = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_TRIGMOD, SIZEOF(RESTRICT_TRIGMOD)))
							restrictions.trigger_mod = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_CENABLE, SIZEOF(RESTRICT_CENABLE)))
							restrictions.cenable = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_DSE, SIZEOF(RESTRICT_DSE)))
							restrictions.dse = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_DIRECT_MODE,
								SIZEOF(RESTRICT_DIRECT_MODE)))
							restrictions.dmode = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_ZCMDLINE, SIZEOF(RESTRICT_ZCMDLINE)))
							restrictions.zcmdline = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_HALT, SIZEOF(RESTRICT_HALT)))
							restrictions.halt_op = restrict_one;
						else if (0 == STRNCASECMP(facility, RESTRICT_ZHALT, SIZEOF(RESTRICT_ZHALT)))
							restrictions.zhalt_op = restrict_one;
						else
						{	/* Parse error - restrict everything */
							send_msg_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_RESTRICTSYNTAX, 3,
									LEN_AND_STR(rfpath), lineno);
							restrict_all = TRUE;
							break;
						}
					}
				} while (!restrict_all && !feof(rfp));
				FCLOSE(rfp, status);
				if (NULL != grpbuf)
					free(grpbuf);
			}
		}
		if (restrict_all)
		{
			restrictions.break_op = TRUE;
			restrictions.zbreak_op = TRUE;
			restrictions.zedit_op = TRUE;
			restrictions.zsystem_op = TRUE;
			restrictions.pipe_open = TRUE;
			restrictions.trigger_mod = TRUE;
			restrictions.cenable = TRUE;
			restrictions.dse = TRUE;
			restrictions.dmode = TRUE;
			restrictions.zcmdline = TRUE;
			restrictions.halt_op = TRUE;
			restrictions.zhalt_op = TRUE;
		}
	}
	restrict_initialized = TRUE;
}
