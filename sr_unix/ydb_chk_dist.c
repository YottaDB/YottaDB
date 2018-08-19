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

/************************************************************************************
  *	Current YottaDB operation on Unix depends on $ydb_dist environment variable
  *	being set correctly or it cannot communicate with gtmsecshr. If this
  *	communication fails, it can cause many problems. Hence this check function
  *	"ydb_chk_dist" which does the following.
  *
  *	1. Ensure $ydb_dist is defined
  *
  *	2. Ensure that the executable (specified in "image" parameter) resides in $ydb_dist.
  *	This check is not done in case the caller is call_ins ("image" is NULL).
  *
  *	This provides enough assurance that $ydb_dist is suitable for use.
  *
  *	This comparison is done using the inode of the executable in $ydb_dist
  *	and the discovered executable path (from /proc/self/exe).
  *	Using argv[0] wasn't possible when $ydb_dist is in the $PATH.
  *
  *	The GT.CM servers, OMI and GNP, always defer issuing an error for an
  *	unverified $ydb_dist. All other executables, MUMPS, MUPIP, DSE, and LKE
  *	issue the error messages as soon as possible.
  *
  *	Note: An addition check of $ydb_dist/gtmsecshr was relocated to "secshr_client".
  ************************************************************************************/

#include "mdef.h"

#include <errno.h>
#include "gtm_string.h"
#include "gtm_stat.h"
#include "gtm_stdlib.h"
#include "gtm_unistd.h"
#include "gtm_limits.h"
#include "gdsroot.h"
#include "error.h"
#include "parse_file.h"
#include "is_file_identical.h"
#include "ydb_chk_dist.h"
#include "gtmimagename.h"
#include "have_crit.h"

#ifdef __APPLE__
# include <mach-o/dyld.h>
#endif

GBLREF	char		ydb_dist[YDB_PATH_MAX];
GBLREF	boolean_t	ydb_dist_ok_to_use;

LITREF	gtmImageName	gtmImageNames[];

error_def(ERR_DISTPATHMAX);
error_def(ERR_SYSCALL);
error_def(ERR_YDBDISTUNDEF);
error_def(ERR_YDBDISTUNVERIF);
error_def(ERR_FILEPARSE);
error_def(ERR_MAXGTMPATH);
error_def(ERR_TEXT);

int ydb_chk_dist(char *image)
{
	char		*real_dist;
	char		*exename;
	int		exename_len;
	int		path_len;
	int		ydb_dist_len;
	boolean_t	status;
	boolean_t	is_gtcm_image;
	char		image_real_path[YDB_PATH_MAX];
	char		real_ydb_dist_path[YDB_PATH_MAX];
	char		comparison[YDB_PATH_MAX];
	int		nbytes;

	is_gtcm_image = (IS_GTCM_GNP_SERVER_IMAGE || IS_GTCM_SERVER_IMAGE); /* GT.CM servers defer issuing errors until startup */
	/* Use the real path while checking the path length. If not valid, let it fail when checking is_file_identical  */
	real_dist = realpath(ydb_dist, real_ydb_dist_path);
	if (real_dist)
		STRNLEN(real_dist, YDB_PATH_MAX, ydb_dist_len);
	else
		STRNLEN(ydb_dist, YDB_PATH_MAX, ydb_dist_len);
	if (ydb_dist_len)
	{
		assert(IS_VALID_IMAGE && (n_image_types > image_type));	/* assert image_type is initialized */
		if (YDB_DIST_PATH_MAX <= ydb_dist_len)
		{
			if (is_gtcm_image)
				return 0;
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_DISTPATHMAX, 1, YDB_DIST_PATH_MAX);
		}
	} else
	{
		if (is_gtcm_image)
			return 0;
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_YDBDISTUNDEF);
	}
	/* Get currently running executable */
	#if defined(__APPLE__)
	uint32_t size = YDB_PATH_MAX;
	char image_path[YDB_PATH_MAX];
	if (_NSGetExecutablePath(image_path, &size) < 0)
	{
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_DISTPATHMAX, 1, YDB_DIST_PATH_MAX); /* Error return from _NSGetExecutablePath */
	}
	nbytes = SNPRINTF(image_real_path, YDB_PATH_MAX, image_path);
	#elif defined(__linux__)
	nbytes = SNPRINTF(image_real_path, YDB_PATH_MAX, PROCSELF);
	#endif

	if ((0 > nbytes) || (nbytes >= YDB_PATH_MAX))
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_DISTPATHMAX, 1, YDB_DIST_PATH_MAX); /* Error return from SNPRINTF */

	/* Create the comparison path (ydb_dist + '/' + exename + '\0') and compare it to image_real_path */
	exename = strrchr(image, '/');
	if (!exename)	/* no slash found, then image is just the exe's name */
		exename = image;
	else		/* slash found in the path, advance the pointer by one to get the name */
		exename++;
	STRNLEN(exename, YDB_PATH_MAX, exename_len);
	SNPRINTF(comparison, YDB_PATH_MAX, "%s/%s", ydb_dist, exename);
	status = is_file_identical(image_real_path, comparison);
	if (!status)
	{
		if (is_gtcm_image)
			return 0;
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_YDBDISTUNVERIF, 4, LEN_AND_STR(ydb_dist), LEN_AND_STR(image));
	}
	ydb_dist_ok_to_use = TRUE;
	return 0;
}
