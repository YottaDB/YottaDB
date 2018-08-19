/****************************************************************
 *								*
 * Copyright (c) 2006-2018 Fidelity National Information	*
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

/* NOTE: THIS MODULE MUST NEVER INCLUDE HEADER fILE gtm_icu_api.h */

#include "mdef.h"

#include "gtm_stdio.h"
#include "gtm_string.h"
#include "gtm_strings.h"
#include "gtm_stdlib.h"		/* needed for realpath */

#include <wctype.h>
#include <dlfcn.h>
#include <locale.h>		/* needed for setlocale() */
#include <langinfo.h>		/* needed for nl_langinfo() */
#ifdef _AIX
#include <gtm_limits.h>		/* needed for YDB_PATH_MAX */
#include <sys/ldr.h>		/* needed for loadquery */
#include <libgen.h>		/* needed for basename */
#include <gtm_stat.h>
#include <errno.h>
#endif

#define GTMIO_MINIMAL		/* If gtmio.h is pulled in for tracing, don't include stuff that causes redefined errors */
#include "error.h"
#include "io.h"
#include "iosp.h"
#include "ydb_trans_log_name.h"
#include "real_len.h"		/* for COPY_DLERR_MSG */
#include "fgncal.h"		/* needed for COPY_DLLERR_MSG() */

#ifdef _AIX
# define DELIM			":"
# define MAX_SEARCH_PATH_LEN	YDB_PATH_MAX * 4 /* Give enough room for loadquery to give the search paths in the first time */
# define ICU_LIBNAME_LEN	YDB_PATH_MAX
# define ICU_NOT_FOUND_ERR	"Cannot find ICU library in the standard system library path (/usr/lib, /usr/lib64 etc.) or LIBPATH"
#endif

ZOS_ONLY(GBLREF	char	*gtm_utf8_locale_object;)
GBLREF	volatile boolean_t	timer_in_handler;

LITREF	char	*ydbenvname[YDBENVINDX_MAX_INDEX];
LITREF	char	*gtmenvname[YDBENVINDX_MAX_INDEX];

typedef void (*icu_func_t)();	/* a generic pointer type to the ICU function */

/* For now the minimum ICU version supported is 3.6 */
#define	ICU_MINIMUM_SUPPORTED_VER	"3.6"
#define IS_ICU_VER_GREATER_THAN_MIN_VER(major_ver, minor_ver) ((3 < major_ver) || ((3 == major_ver) && (6 <= minor_ver)))

/* ICU function to query for the ICU version. Used only if $ydb_icu_version is not set. */
#define GET_ICU_VERSION_FNAME	"u_getVersion"

/* Indicates the maximum size of the array that is given as input to u_getVersion. The corresponding MACRO
 * in unicode/uversion.h is U_MAX_VERSION_LENGTH and is tagged as @stable 2.4.
 */
#define MAX_ICU_VERSION_LENGTH	4

/* Indicates the maximum length of the complete ICU version in string form. The corresponding MACRO in
 * unicode/uversion.h is U_MAX_VERSION_STRING_LENGTH and is tagged as @stable 2.4.
 */
#define MAX_ICU_VERSION_STRLEN	20

/* Maintain max length of all the ICU function names that YottaDB uses. This is currently 20 so we keep max at 24 to be safe.
 * In case a new function that has a name longer than 24 chars gets added, an assert below will fail so we are protected.
 */
#define MAX_ICU_FNAME_LEN	24

/* The function u_getVersion which queries for the version number of installed ICU library expects
 * UVersionInfo which is typefed to uint8_t. To avoid including unicode/uversion.h that defines UVersionInfo
 * do explicit typedef of uint8_t to UVersionInfo here. We don't expect UVersionInfo type to change as it is
 * tagged as @stable ICU 2.4 and YottaDB will only support ICU versions >= 3.6
 */
typedef uint8_t UVersionInfo[MAX_ICU_VERSION_LENGTH];

/* The first parameter to ICUVERLT36 can be either ICU_LIBNAME or $ydb_icu_version. Depending on the choice
 * of the first parameter, different suffix is chosen.
 */
#define ICU_LIBNAME_SUFFIX		" has version"

/* Declare enumerators for all functions */
#define ICU_DEF(x) x##_,
enum {
#include "gtm_icu.h"	/* BYPASSOK */
icu_func_n		/* total number of ICU functions used */
};
#undef ICU_DEF

/* Initialize the table of symbol names to be used in dlsym() */
#define ICU_DEF(x) #x,
LITDEF char* icu_fname[] =
{
#include "gtm_icu.h"	/* BYPASSOK */
NULL
};
#undef ICU_DEF

#define ICU_DEF(x) GBLDEF icu_func_t x##_ptr;
#include "gtm_icu.h"
#undef ICU_DEF

/* initialize the table of locations of function pointers that are set by dlsym() */
#define ICU_DEF(x) &x##_ptr,
GBLDEF icu_func_t *icu_fptr[] =
{
#include "gtm_icu.h"	/* BYPASSOK */
NULL
};
#undef ICU_DEF

/* duplicated prototypes needed to avoid including header file gtm_icu_api.h */
void gtm_icu_init(void);
void gtm_conv_init(void);


error_def(ERR_DLLNOOPEN);
error_def(ERR_ICUSYMNOTFOUND);
error_def(ERR_TEXT);
error_def(ERR_NONUTF8LOCALE);
error_def(ERR_ICUVERLT36);

/*
 * The ICU project has used two different formats to specify the version. These format are visible to us
 * through icu-config, the library name and renamed symbols.
 *
 * icu-config reported the version in the following format from ICU 3.6 until ICU 4.8
 * 	<major> . <minor> . <sub minor vers...>
 * As of ICU 49 (aka 4.9), the format returned is:
 * 	<major><minor> . <sub minor vers...>
 *
 *
 * The version numbers used in the ICU library file names from ICU 3.6 until ICU 4.8 were
 * 	<major> . <minor> . <sub minor vers...>	(symlinked to the below name)
 * 	<major> . <minor>
 *
 * As of ICU 49 (aka 4.9) the version used in the library file names is (note the missing dot between major and minor)
 * 	<major><minor> . <sub minor vers...>	(symlinked to the below name)
 * 	<major><minor>
 *
 *
 * The version number extension for renamed symbols from ICU 3.6 until ICU 4.2 was (confirmed on
 * http://icu-project.org/apiref/icu4c/uvernum_8h.html, search for U_ICU_ENTRY_POINT_RENAME)
 * 	_<major> _ <minor>
 * As of ICU 4.4, the version number extension changed to (note the missing underscore)
 * 	_<major><minor>
 *
 * The function below parses ydb_icu_version to determine the strings used in the library and symbol versions
 */
static boolean_t parse_ydb_icu_version(char *icu_ver_buf, int len, char *icusymver, char *iculibver, boolean_t is_ydb_env_match)
{
	char		*ptr;
	char		tmp_errstr[128]; /* "$ydb_icu_version is" */
	int4		major_ver, minor_ver;
	int		i;

	if ((NULL == icu_ver_buf) || (0 == len))
		return FALSE;	/* empty string */

	/* Deconstruct the two known forms of ydb_icu_version "[0-9].[0-9]" and "[0-9][0-9]" ignoring trailing values */
	ptr = icu_ver_buf;
	if (-1 == (major_ver = asc2i((uchar_ptr_t)ptr++, 1)))
		return FALSE;
	if ('.' == *ptr)
		ptr++;
	if (-1 == (minor_ver = asc2i((uchar_ptr_t)ptr, 1)))
		return FALSE;

	/* Generate the ICU symbol renaming string */
	i = 0;
	icusymver[i++] = '_';
	icusymver[i++] = *icu_ver_buf;
	if ( 44 > ((major_ver * 10) + minor_ver))
		icusymver[i++] = '_';
	icusymver[i++] = *ptr;
	icusymver[i++] = '\0';

	/* Generate the ICU library name string */
	i = 0;
	iculibver[i++] = *icu_ver_buf;
	iculibver[i++] = *ptr;
	iculibver[i++] = '\0';

	/* Check if the formatted version is greater than or equal to 3.6 */
	if (!(IS_ICU_VER_GREATER_THAN_MIN_VER(major_ver, minor_ver)))
	{
		/* Construct the first part of the ICUVERLT36 error message. */
		SPRINTF(tmp_errstr, "%s is",
			(is_ydb_env_match ?  ydbenvname[YDBENVINDX_ICU_VERSION] : gtmenvname[YDBENVINDX_ICU_VERSION]));
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_ICUVERLT36, 4, LEN_AND_STR(tmp_errstr), major_ver, minor_ver);
	}
	return TRUE;
}

void gtm_icu_init(void)
{
	char		*locale, *chset, *libname, err_msg[MAX_ERRSTR_LEN];
	char		icu_final_fname[MAX_ICU_FNAME_LEN + 1 + MAX_ICU_VERSION_STRLEN];	/* 1 for '_' in between */
	char		icu_ver_buf[MAX_ICU_VERSION_STRLEN], icusymver[MAX_ICU_VERSION_STRLEN], iculibver[MAX_ICU_VERSION_STRLEN];
	char		tmp_errstr[SIZEOF(ICU_LIBNAME) + STR_LIT_LEN(ICU_LIBNAME_SUFFIX)]; /* "libicuio.so has version" */
	char		icu_libname[SIZEOF(ICU_LIBNAME) + MAX_ICU_VERSION_STRLEN];
	char		*strtokptr;
	const char	*cur_icu_fname;
	int		icu_final_fname_len, icu_libname_len, len, save_fname_len, icusymver_len, iculibver_len;
	void_ptr_t	handle;
	char_ptr_t	err_str;
	icu_func_t	fptr;
	int		findx;
	boolean_t	icu_getversion_found = FALSE, gtm_icu_ver_defined, symbols_renamed;
	UVersionInfo	icu_version;
	mstr		trans;
	char		*envname;
#	ifdef _AIX
	int		buflen, prev_dyn_size;
	char            buf[ICU_LIBNAME_LEN], temp_path[YDB_PATH_MAX], real_path[YDB_PATH_MAX], search_paths[MAX_SEARCH_PATH_LEN];
	char		*ptr, *each_libpath, *dyn_search_paths = NULL, *search_path_ptr;
	struct stat	real_path_stat;		/* To see if the resolved real_path exists or not */
#	endif
	boolean_t	is_ydb_env_match;

	assert(!gtm_utf8_mode);
#	ifdef __MVS__
	if (gtm_utf8_locale_object)
		locale = setlocale(LC_CTYPE, gtm_utf8_locale_object);
	else
#	endif
	locale = setlocale(LC_CTYPE, "");
	chset = nl_langinfo(CODESET);
	if (NULL == chset)	/* 4SCA : null return nl_langinfo not possible */
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_NONUTF8LOCALE, 2, LEN_AND_LIT("<undefined>"));
	if ((NULL == locale) || (0 != strcasecmp(chset, "utf-8")) && (0 != strcasecmp(chset, "utf8")))
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_NONUTF8LOCALE, 2, LEN_AND_STR(chset));
	/* By default, YottaDB will henceforth expect that ICU has been built with symbol renaming disabled. If that is not the case,
	 * YottaDB can be notified of this through an environment variable ($ydb_icu_version).  The variable should contain the
	 * icu version number formatted as "<major-ver>.<minor-ver>". Example would be "3.6" to indicate ICU 3.6.
	 * Given the above environment variable, YottaDB will try to dlopen the version specific icu library directly
	 * (i.e. libicuio.so.36 instead of libicuio.so). In that case, it will try both renamed and non-renamed symbols and
	 * whichever works it will use that. But if the environment variable is not specified, YottaDB will assume that ICU has
	 * been built with no renaming of symbols. Any value of the environment variable not conforming to the above formatting
	 * will be treated as if this environment variable was not set at all and the default behavior (which is to query for
	 * symbols without appended version numbers) will be used.
	 */
	gtm_icu_ver_defined = FALSE;
	if (SS_NORMAL == ydb_trans_log_name(YDBENVINDX_ICU_VERSION, &trans, icu_ver_buf, SIZEOF(icu_ver_buf),
											IGNORE_ERRORS_TRUE, &is_ydb_env_match))
	{	/* $ydb_icu_version is defined. Do edit check on the value before considering it really defined */
		gtm_icu_ver_defined = parse_ydb_icu_version(trans.addr, trans.len, icusymver, iculibver, is_ydb_env_match);
	}
	if (gtm_icu_ver_defined)
	{	/* User explicitly specified an ICU version. So load version specific icu file (e.g. libicuio.so.36) */
		icu_libname_len = 0;
		iculibver_len = STRLEN(iculibver);
		icusymver_len = STRLEN(icusymver);
#		if defined(_AIX) || defined(__MVS__) || defined(__CYGWIN__)
		/* Transform (e.g. libicuio.a  -> libicuio36.a  ) */
		len = STR_LIT_LEN(ICU_LIBNAME_ROOT);
		memcpy(&icu_libname[icu_libname_len], ICU_LIBNAME_ROOT, len);
		icu_libname_len += len;
		memcpy(&icu_libname[icu_libname_len], iculibver, iculibver_len);
		icu_libname_len += iculibver_len;
		icu_libname[icu_libname_len++] = '.';
		len = STR_LIT_LEN(ICU_LIBNAME_EXT);
		memcpy(&icu_libname[icu_libname_len], ICU_LIBNAME_EXT, len);
		icu_libname_len += len;
#		elif defined(__APPLE__)
		/* Transform (e.g. libicuio.a  -> libicuio.55.1.dylib) */
		len = STR_LIT_LEN(ICU_LIBNAME_ROOT);
		memcpy(&icu_libname[icu_libname_len], ICU_LIBNAME_ROOT, len);
		icu_libname_len += len;
		icu_libname[icu_libname_len++] = '.';
		memcpy(&icu_libname[icu_libname_len], iculibver, iculibver_len);
		icu_libname_len += iculibver_len;
		icu_libname[icu_libname_len++] = '.';
		len = STR_LIT_LEN(ICU_LIBNAME_EXT);
		memcpy(&icu_libname[icu_libname_len], ICU_LIBNAME_EXT, len);
		icu_libname_len += len;
#		else
		/* Transform (e.g. libicuio.so -> libicuio.so.36) */
		len = STR_LIT_LEN(ICU_LIBNAME);
		memcpy(&icu_libname[icu_libname_len], ICU_LIBNAME, len);
		icu_libname_len += len;
		icu_libname[icu_libname_len++] = '.';
		memcpy(&icu_libname[icu_libname_len], iculibver, iculibver_len);
		icu_libname_len += iculibver_len;
#		endif
		icu_libname[icu_libname_len] = '\0';
		assert(SIZEOF(icu_libname) > icu_libname_len);
		libname = icu_libname;
	} else
#if defined(__APPLE__)
		libname = APPLE_ICU_LIB_NAME; /* Use Apple's undocumented private ICU implementation with unrenamed symbols */
#else
		libname = ICU_LIBNAME;	/* go with default name */
#endif
#	ifdef _AIX
	if (gtm_icu_ver_defined || /* Use the AIX system default when no ICU version specified */
			NULL == (handle = dlopen(ICU_LIBNAME_DEF, ICU_LIBFLAGS | RTLD_MEMBER)))
	{
		/* AIX has a unique packaging convention in that shared objects are conventionally
		 * archived into a static (.a) library. To resolve the shared library name at runtime
		 * in a version independent way we use loadquery to fetch the paths that might contain
		 * the ICU shared library. By running through each of these paths, we see if any of them
		 * contains the libicuio.a. If so, we use realpath to find the versioned ICU library
		 * that is symbolically linked from libicuio.a. This realpath can then be used to construct
		 * the fully qualified archive + member combination that will be finally dlopen'ed. */
		prev_dyn_size = MAX_SEARCH_PATH_LEN;
		search_path_ptr = search_paths;
		while(-1 == loadquery(L_GETLIBPATH, search_path_ptr, prev_dyn_size))
		{
			/* We don't expect loadquery to fail for reason other than ENOMEM */
			assertpro(ENOMEM == errno);
			/* If the previous call to loadquery fails and if it's because the input buffer's length was not
			 * enough for loadquery to fill the library search paths, then do a malloc equal to double the previous
			 * size and call loadquery again. It's relatively unlikely that this condition would be reached
			 */
			if (NULL != dyn_search_paths)
				free(dyn_search_paths);
			prev_dyn_size *= 2;
			dyn_search_paths = (char *)malloc(prev_dyn_size);
			search_path_ptr = dyn_search_paths;
		}
		/* At this point we have all the library search paths pointed by search_path_ptr seperated by ":". */
		each_libpath = STRTOK_R(search_path_ptr, DELIM, &strtokptr);
		while (NULL != each_libpath)
		{
			SNPRINTF(temp_path, YDB_PATH_MAX, "%s/%s", each_libpath, libname);
			if (NULL == realpath(temp_path, real_path) && (0 != Stat(real_path, &real_path_stat)))
			{
				each_libpath = STRTOK_R(NULL, DELIM, &strtokptr);
				continue;
			}
			/* At this point we would have in real_path the fully qualified path to the version'ed libicuio archive.
			 * ICU_LIBNAME - libicuio.a would have resulted in libicuio36.0.a
			 * Now, we need to construct the archive library name along with it's shared object member. This is done
			 * below.
			 */
			buflen = 0;
			/* real_path = /usr/local/lib64/libicuio36.0.a */
			ptr = basename(real_path);
			/* buf = /usr/local/lib64/libicuio36.0.a(libicuio36.0.a */
			SNPRINTF(buf, ICU_LIBNAME_LEN, "%s(%s", real_path, ptr);
			buflen += (STRLEN(real_path) + STRLEN(ptr) + 1);
			ptr = strrchr(buf, '.');
			strcpy(ptr, ".so)");			/* buf = /usr/local/lib64/libicuio36.0.a(libicuio36.0.so) */
			buflen += STR_LIT_LEN(".so)");
			buf[buflen] = '\0';			/* NULL termination */
			break;
		}
		if (NULL != dyn_search_paths)
			free(dyn_search_paths);
		/* If each_libpath is NULL then we were not able to look for libicuio.a in the loader search path */
		if (NULL == each_libpath)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_DLLNOOPEN, 2, LEN_AND_STR(libname),
					ERR_TEXT, 2, LEN_AND_LIT(ICU_NOT_FOUND_ERR));
		libname = buf;
		handle = dlopen(libname, ICU_LIBFLAGS | RTLD_MEMBER);
	}
#else
	handle = dlopen(libname, ICU_LIBFLAGS);
#endif
	if (NULL == handle)
	{
		COPY_DLLERR_MSG(err_str, err_msg);
#		ifdef _AIX
		/* On AIX, ICU is sometimes packaged differently where the archived shared library is named as libicuio.so
		 * instead of libicuio36.0.so. Try dlopen with this new naming scheme as well.
		 * Below SNPRINTF converts /usr/local/lib64/libicuio36.0.a to /usr/local/lib64/libicuio36.0.a(libicuio.so)
		 */
		SNPRINTF(temp_path, ICU_LIBNAME_LEN, "%s(%s.so)", real_path, ICU_LIBNAME_ROOT);
		libname = temp_path;
		handle = dlopen(libname, ICU_LIBFLAGS | RTLD_MEMBER);
		if (NULL == handle)
		{
			libname = buf;
#		endif
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_DLLNOOPEN, 2, LEN_AND_STR(libname),
					ERR_TEXT, 2, LEN_AND_STR(err_msg));
#		ifdef _AIX
		}
#		endif
	}
#	ifdef __hpux
	/* HP-UX dlsym() doesn't allow lookup for symbols that are present in the nested dependent shared libraries
	 * of ICU_LIBNAME. Workaround is to lookup within the global space (i.e. from invoking module libyottadb)
	 * where all symbols would have been brought in by previous dlopen() with the RTLD_GLOBAL flag.
	 */
	handle = dlopen(NULL, ICU_LIBFLAGS);
	assertpro(handle);
#	endif
	DEBUG_ONLY(symbols_renamed = -1;)
	for (findx = 0; findx < icu_func_n; ++findx)
	{
		cur_icu_fname = icu_fname[findx];
		icu_final_fname_len = 0;
		len = STRLEN(cur_icu_fname);
		assert(MAX_ICU_FNAME_LEN > len);	/* ensure we have enough space to hold the icu function */
		memcpy(&icu_final_fname[icu_final_fname_len], cur_icu_fname, len);
		icu_final_fname_len += len;
		icu_final_fname[icu_final_fname_len] = '\0';
		assert(SIZEOF(icu_final_fname) > icu_final_fname_len);
		fptr = NULL;
		assert((0 != findx) || (-1 == symbols_renamed));
		assert((0 == findx) || (FALSE == symbols_renamed) || (TRUE == symbols_renamed));
		if ((0 == findx) || !symbols_renamed)
#ifdef __CYGWIN__ /* Don't ask why... I have no idea how all the funcs are just in the global space in Cygwin */
			fptr = (icu_func_t)dlsym(NULL, icu_final_fname);
#else
			fptr = (icu_func_t)dlsym(handle, icu_final_fname);
#endif
		if (NULL == fptr)
		{	/* If ydb_icu_version is defined to a proper value, then try function name with <major_ver>_<minor_ver> */
			if (gtm_icu_ver_defined && ((0 == findx) || symbols_renamed))
			{
				memcpy(&icu_final_fname[icu_final_fname_len], icusymver, icusymver_len);
				icu_final_fname_len += icusymver_len;
				icu_final_fname[icu_final_fname_len] = '\0';
				assert(SIZEOF(icu_final_fname) > icu_final_fname_len);
#ifdef __CYGWIN__
				fptr = (icu_func_t)dlsym(NULL, icu_final_fname);
#else
				fptr = (icu_func_t)dlsym(handle, icu_final_fname);
#endif
			}
			if (NULL == fptr)
			{
				COPY_DLLERR_MSG(err_str, err_msg);
				envname = (char *)(is_ydb_env_match
							? ydbenvname[YDBENVINDX_ICU_VERSION]
							: gtmenvname[YDBENVINDX_ICU_VERSION]) + 1;	/* + 1 to skip past '$' */
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(10)
					ERR_ICUSYMNOTFOUND, 4, LEN_AND_STR(cur_icu_fname), LEN_AND_STR(envname),
					ERR_TEXT, 2, LEN_AND_STR(err_msg));
			}
			if (0 == findx)	/* record the fact that the symbols ARE renamed */
				symbols_renamed = TRUE;
		} else if (0 == findx)	/* record the fact that the symbols are NOT renamed */
			symbols_renamed = FALSE;
		assert((0 == findx) || icu_getversion_found || gtm_icu_ver_defined); /* u_getVersion should have been dlsym'ed */
		*icu_fptr[findx] = fptr;
		/* If the current function that is dlsym'ed is u_getVersion, then we use fptr to query for the library's ICU
		 * version. If it is less than the least ICU version that YottaDB supports we issue an error. If not, we continue
		 * dlsym the rest of the functions. To facilitate issuing wrong version error early, the ICU function getVersion
		 * should be the first function in gtm_icu.h. This way dlsym on u_getVersion will happen as the first thing in
		 * this loop. But do all this only if ydb_icu_version is not defined in the environment. If it's defined to an
		 * an appropriate value in the environment then the version check would have happened before and there isn't any
		 * need to repeat it again.
		 */
		if (!gtm_icu_ver_defined && (FALSE == icu_getversion_found) && (0 == strcmp(cur_icu_fname, GET_ICU_VERSION_FNAME)))
		{
			icu_getversion_found = TRUE;
			memset(icu_version, 0, MAX_ICU_VERSION_LENGTH);
			fptr(icu_version);
			if (!(IS_ICU_VER_GREATER_THAN_MIN_VER(icu_version[0], icu_version[1])))
			{
				/* Construct the first part of the ICUVERLT36 error message. */
				SPRINTF(tmp_errstr, "%s%s", ICU_LIBNAME, ICU_LIBNAME_SUFFIX);
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_ICUVERLT36, 4,
						LEN_AND_STR(tmp_errstr), icu_version[0], icu_version[1]);
			}
		}
	}
	gtm_utf8_mode = TRUE;
	/* gtm_wcswidth()/U_ISPRINT() in util_format() can henceforth be safely called now that ICU initialization is complete */
	gtm_conv_init();
}
