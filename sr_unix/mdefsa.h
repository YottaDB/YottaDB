/****************************************************************
 *								*
 * Copyright (c) 2001-2016 Fidelity National Information	*
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

#ifndef MDEFSA_included
#define MDEFSA_included

/* Declarations common to all unix mdefsp.h, to be moved here */

/* DSK_WRITE_NOCACHE macro needs <errno.h> to be included. Use this flavor if writing direct from storage (not cache buffer) */
#define	DSK_WRITE_NOCACHE(reg, blk, ptr, odv, status)		\
MBSTART {							\
	if (-1 == dsk_write_nocache(reg, blk, ptr, odv))	\
		status = errno;					\
	else							\
		status = 0;					\
} MBEND

#define DOTM			".m"
#define DOTOBJ			".o"
#define YDB_IMAGE_NAME		"mumps"
#define YDB_IMAGE_NAMELEN	(SIZEOF(YDB_IMAGE_NAME) - 1)
#define	GTMSECSHR_NAME		"gtmsecshr"
#define GTMSECSHR_NAMELEN	(SIZEOF(GTMSECSHR_NAME) - 1)

#define	ICU_LIBFLAGS		(RTLD_NOW | RTLD_GLOBAL)

#define	ICU_LIBNAME_ROOT	"libicuio"
#if defined(__APPLE__)
#	define YOTTADB_IMAGE_NAME	"libyottadb.dylib"
#	define GTMSHR_IMAGE_NAME	"libgtmshr.dylib"
#	define ICU_LIBNAME_EXT		"dylib"
#	define APPLE_ICU_LIB_NAME	"libicucore.dylib"
#else
#	define YOTTADB_IMAGE_NAME	"libyottadb.so"
#	define GTMSHR_IMAGE_NAME	"libgtmshr.so"
#	define ICU_LIBNAME_EXT		"so"
#endif
#define	ICU_LIBNAME		ICU_LIBNAME_ROOT "." ICU_LIBNAME_EXT

#define GTM_MAIN_FUNC		"gtm_main"

/* Prefix GT.M callback functions with "gtm_" */
#define GTM_PREFIX(func)	gtm_##func
#define cancel_timer		GTM_PREFIX(cancel_timer)
#define hiber_start		GTM_PREFIX(hiber_start)
#define hiber_start_wait_any	GTM_PREFIX(hiber_start_wait_any)

#endif /* MDEFSA_included */
