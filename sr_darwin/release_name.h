/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.*
 * All rights reserved.						*
 *								*
 * Copyright (c) 2017-2018 Stephen L Johnson.			*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef GTM_RELEASE_NAME

#define GTM_VERSION		"V6.3"
#define	GTM_ZVERSION		"V6.3-004"
#define	YDB_ZYRELEASE		"r1.23"

/* Note: YDB_RELEASE_STAMP is set as part of the cmake build process.
 * Example values are
 *	#define YDB_RELEASE_STAMP	"20180316 15:27"
 */

#if defined(__x86_64__)
# define YDB_PLATFORM		"Darwin x86_64"
#elif defined(__armv6l__)
# define YDB_PLATFORM		"Darwin armv6l"
#elif defined(__armv7l__)
# define YDB_PLATFORM		"Darwin armv7l"
#else
# define YDB_PLATFORM		"Darwin x86"
#endif

#define GTM_RELEASE_NAME 	"GT.M" " " GTM_ZVERSION " " YDB_PLATFORM
#define YDB_RELEASE_NAME 	"YottaDB" " " YDB_ZYRELEASE " " YDB_PLATFORM
#define	YDB_AND_GTM_RELEASE_NAME	GTM_RELEASE_NAME " " "YottaDB" " " YDB_ZYRELEASE
#define GTM_PRODUCT 		"GT.M"
#define YDB_PRODUCT		"YottaDB"

#endif
