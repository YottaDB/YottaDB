/****************************************************************
 *								*
 *	Copyright 2001 Sanchez Computer Associates, Inc.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"
#include "gtm_unistd.h"
#include "get_page_size.h"

GBLDEF int4	gtm_os_page_size;

void get_page_size(void)
{
	#ifndef __APPLE__
	gtm_os_page_size = getpagesize();
	#else
	gtm_os_page_size = sysconf(_SC_PAGESIZE);
	#endif

	return;
}
