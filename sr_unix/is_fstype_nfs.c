/****************************************************************
 *								*
 * Copyright (c) 2016-2017 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/
#include "mdef.h"

#ifndef __APPLE__
#include <sys/statfs.h>
#endif
#ifdef _AIX
#include <sys/vmount.h>	/* needed for MNT_NFS */
#endif

#if defined( __linux__) || defined(__CYGWIN__)
/* Define NFS_SUPER_MAGIC. This should ideally include <linux/nfs_fs.h> for NFS_SUPER_MAGIC.
 * However, this header file doesn't seem to be standard and gives lots of
 * compilation errors and hence defining again here.  The constant value
 * seems to be portable across all linuxes (courtesy 'statfs' man pages)
 */
# define NFS_SUPER_MAGIC 0x6969
#endif

#include "is_fstype_nfs.h"

boolean_t is_fstype_nfs(int fd)
{
#	ifdef __APPLE__
	return FALSE;
#	else
	struct statfs	buf;
	boolean_t	is_nfs;

	is_nfs = FALSE;
	if (0 != fstatfs(fd, &buf))
		return is_nfs;
#	if defined(__linux__) || defined(__CYGWIN__)
	is_nfs = (NFS_SUPER_MAGIC == buf.f_type);
#	elif defined(_AIX)
	is_nfs = (MNT_NFS == buf.f_vfstype);
#	else
#	error UNSUPPORTED PLATFORM
#	endif
	return is_nfs;
#	endif
}
