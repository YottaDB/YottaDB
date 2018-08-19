/****************************************************************
 *								*
 * Copyright 2001, 2014 Fidelity Information Services, Inc	*
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

#define BYPASS_MEMCPY_OVERRIDE	/* Signals gtm_string.h to not override memcpy(). This causes linking problems when libmumps.a
				 * is not available.
				 */
#include "main_pragma.h"

#undef UNIX		/* Causes non-YottaDB-runtime routines (libyottadb) to be used since libyottadb is not yet available */
#include "gtm_stdio.h"
#define UNIX
#include "gtm_string.h"
#include "gtm_strings.h"
#include "gtm_stdlib.h"
#include "gtm_limits.h"

#include <dlfcn.h>
#include <errno.h>

#include "ydb_chk_dist.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef DEBUG
# include <sys/types.h>	/* needed by "assert" macro */
# include <signal.h>	/* needed by "assert" macro */
# include <unistd.h>	/* needed by "assert" macro */
#endif

/* Define "assert" macro (cannot use normal assert as that needs "rts_error_csa" which we don't want to pull in here) */
#undef assert	/* in case it is defined */
#ifdef DEBUG
# define assert(x) ((x) ? 1 : (fprintf(stderr, "Assert failed at %s line %d : %s\n", __FILE__, __LINE__, #x), fflush(stderr), fflush(stdout), kill(getpid(), SIGILL)))
#else
# define assert(x)
#endif

typedef int (*exe_main_t)(int argc, char **argv, char **envp);

int dlopen_libyottadb(int argc, char **argv, char **envp, char *main_func)
{
	int		status, pathlen, save_errno;
	char		*pathptr, *tmpptr;	/* Includes null terminator char */
	char		curr_exe_realpath[YDB_PATH_MAX], libyottadb_realpath[YDB_PATH_MAX];
	void_ptr_t	handle;
	exe_main_t	exe_main;

	/* We fake the output of the following messages to appear as if they were reported by YottaDB error
	 * handlers (gtm_putmsg/rts_error) by prefixing every message with %YDB-E-*
	 *   ERR_YDBDISTUNDEF
	 *   ERR_DISTPATHMAX
	 *   ERR_DLLNOOPEN
	 *   ERR_DLLNORTN
	 */
	/* Determine the path of the executable and use that to set the environment variable "ydb_dist"
	 * (and "gtm_dist", supported for backward compatibility). Note that we cannot use argv[0] in case
	 * the executable was invoked without specifying an absolute or relative path (e.g. using $PATH in shell).
	 */
	/* Get currently running executable */
	#if defined(__APPLE__)
	uint32_t size = YDB_PATH_MAX;
	char curr_exe_path[YDB_PATH_MAX];
	if (_NSGetExecutablePath(curr_exe_path, &size) < 0)
	{
		FPRINTF(stderr, "%%YDB-E-DISTPATHMAX, Executable path length is greater than maximum (%d)\n", YDB_DIST_PATH_MAX);
		return ERR_DISTPATHMAX;
	}
	pathptr = realpath(curr_exe_path, curr_exe_realpath);
	#elif defined(__linux__)
	pathptr = realpath(PROCSELF, curr_exe_realpath);
	#endif
	if (NULL != pathptr)
	{
		pathlen = STRLEN(pathptr);
		assert(DIR_SEPARATOR == pathptr[0]);
		assert(pathlen);
		tmpptr = pathptr + pathlen - 1;
		while (DIR_SEPARATOR != *tmpptr)
			tmpptr--;
		assert(tmpptr >= pathptr);
		/* At this point "tmpptr" points to the last '/' */
		/* At this point "pathptr" points to the pathname. Now check if PATH + "/libyottadb.so" can fit in YDB_PATH_MAX */
		if (YDB_DIST_PATH_MAX < ((tmpptr - pathptr) + STR_LIT_LEN(DIR_SEPARATOR) + STR_LIT_LEN(YOTTADB_IMAGE_NAME) + 1))
			pathptr = NULL;	/* so we issue a DISTPATHMAX error below */
	}
	if (NULL == pathptr)
	{
		FPRINTF(stderr, "%%YDB-E-DISTPATHMAX, Executable path length is greater than maximum (%d)\n", YDB_DIST_PATH_MAX);
		return ERR_DISTPATHMAX;
	}
	/* Now set "ydb_dist" (and "gtm_dist") to the obtained canonical path. "pathptr" points to that.
	 * Note that we cannot ydbenvname[YDBENVINDX_DIST] and gtmenvname[YDBENVINDX_DIST] like is used in gtmci.c
	 * because those are available in libyottadb.so which is not yet open at this point.
	 */
	assert(DIR_SEPARATOR == *tmpptr);
	*tmpptr = '\0';
	status = setenv("ydb_dist", pathptr, TRUE);
	if (status)
	{
		assert(-1 == status);
		save_errno = errno;
		FPRINTF(stderr, "%%YDB-E-SYSCALL, Error received from system call setenv(ydb_dist) -- called from module "
			"%s at line %d\n%%SYSTEM-E-ENO%d, %s\n", __FILE__, __LINE__, save_errno, STRERROR(save_errno));
		return ERR_SYSCALL;
	}
	status = setenv("gtm_dist", pathptr, TRUE);
	if (status)
	{
		assert(-1 == status);
		save_errno = errno;
		FPRINTF(stderr, "%%YDB-E-SYSCALL, Error received from system call setenv(gtm_dist) -- called from module "
			"%s at line %d\n%%SYSTEM-E-ENO%d, %s\n", __FILE__, __LINE__, save_errno, STRERROR(save_errno));
		return ERR_SYSCALL;
	}
	/* Get curr_exe_realpath back to its correct form (with the executable name at the end) */
	*tmpptr++ = DIR_SEPARATOR;
	/* Now open libyottadb.so */
	assert(SIZEOF(libyottadb_realpath) == SIZEOF(curr_exe_realpath));
	memcpy(libyottadb_realpath, curr_exe_realpath, tmpptr - curr_exe_realpath);
	tmpptr = libyottadb_realpath + (tmpptr - curr_exe_realpath);
	memcpy(tmpptr, YOTTADB_IMAGE_NAME, STR_LIT_LEN(YOTTADB_IMAGE_NAME));
	tmpptr[STR_LIT_LEN(YOTTADB_IMAGE_NAME)] = 0;

	/* RTLD_NOW - resolve immediately so we know errors sooner than later
	 * RTLD_GLOBAL - make all exported symbols from libyottadb.so available for subsequent dlopen.
	 */
	handle = dlopen(&libyottadb_realpath[0], (RTLD_NOW | RTLD_GLOBAL));
	if (NULL == handle)
	{
		FPRINTF(stderr, "%%YDB-E-DLLNOOPEN, Failed to load external dynamic library %s\n", libyottadb_realpath);
		if ((tmpptr = dlerror()) != NULL)
			FPRINTF(stderr, "%%YDB-E-TEXT, %s\n", tmpptr);
		return ERR_DLLNOOPEN;
	}
	status = 0;
	exe_main = (exe_main_t)dlsym(handle, main_func);
	if (NULL == exe_main)
	{
		FPRINTF(stderr, "%%YDB-E-DLLNORTN, Failed to look up the location of the symbol %s\n", main_func);
		if ((tmpptr = dlerror()) != NULL)
			FPRINTF(stderr, "%%YDB-E-TEXT, %s\n", tmpptr);
		return ERR_DLLNORTN;
	}
	/* Switch argv[0] to canonical full path of executable returned by the "realpath" call above.
	 * When argv[0] is later used in "ydb_chk_dist", we need the canonical path to avoid incorrect YDBDISTUNVERIF errors.
	 */
	argv[0] = curr_exe_realpath;
	status = exe_main(argc, argv, envp);
	return status;
}
