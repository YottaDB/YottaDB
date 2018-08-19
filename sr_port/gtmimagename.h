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

#ifndef GTMIMAGENAME_DEF
#define GTMIMAGENAME_DEF

typedef struct
{
	char	*imageName;
	int	imageNameLen;
} gtmImageName;

enum gtmImageTypes
{
#define IMAGE_TABLE_ENTRY(A,B)	A,
#include "gtmimagetable.h"
#undef IMAGE_TABLE_ENTRY
	n_image_types
};

LITREF	gtmImageName	gtmImageNames[];

#define GTMIMAGENAMETXT(x) gtmImageNames[x].imageNameLen, gtmImageNames[x].imageName

GBLREF	enum gtmImageTypes	image_type;	/* needed by IS_MUMPS_IMAGE and IS_GTM_IMAGE macros */
GBLREF	boolean_t		run_time;	/* needed by IS_MCODE_RUNNING macro */

#define IS_MCODE_RUNNING	(run_time)

#define IS_DSE_IMAGE			(DSE_IMAGE == image_type)
#define IS_GTCM_SERVER_IMAGE		(GTCM_SERVER_IMAGE == image_type)
#define IS_GTCM_GNP_SERVER_IMAGE	(GTCM_GNP_SERVER_IMAGE == image_type)
#define IS_GTMSECSHR_IMAGE		(GTMSECSHR_IMAGE == image_type)
#define IS_GTM_IMAGE			IS_MUMPS_IMAGE
#define IS_LKE_IMAGE			(LKE_IMAGE == image_type)
#define IS_MUMPS_IMAGE			(GTM_IMAGE == image_type)
#define IS_MUPIP_IMAGE			(MUPIP_IMAGE == image_type)
#define IS_GTCM_SHMCLEAN_IMAGE		(GTCM_SHMCLEAN_IMAGE == image_type)
#define	IS_VALID_IMAGE			(INVALID_IMAGE != image_type)

/* Define macros that are helpful in verifying that functions corresponding to file names that previously
 * used to be in .list files continue to be invoked only for those images and not by anything else.
 * For example, libgnpclient.list used to have a list of modules that were linked only by mumps and lke.
 * So define IS_LIBGNPCLIENT macro for this case. Likewise for other former .list files.
 */
#define IS_LIBGNPCLIENT		(IS_MUMPS_IMAGE | IS_LKE_IMAGE)
#define	IS_LIBGNPSERVER		(IS_GTCM_GNP_SERVER_IMAGE)
#define	IS_LIBCMISOCKETTCP	(IS_MUMPS_IMAGE || IS_LKE_IMAGE || IS_GTCM_GNP_SERVER_IMAGE)
#define	IS_LIBGTCM		(IS_GTCM_SERVER_IMAGE || IS_GTCM_SHMCLEAN_IMAGE)

#ifdef __APPLE__
#define LIBYOTTADBDOTSO		"%s/libyottadb.dylib"
#else
#define LIBYOTTADBDOTSO		"%s/libyottadb.so"
#endif
#endif
