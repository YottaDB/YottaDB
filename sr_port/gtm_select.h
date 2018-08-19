/****************************************************************
 *								*
 *	Copyright 2001, 2013 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/*	gtm_select.h - include Unix system header files needed by select().  */

#if	defined(_AIX) || defined(__CYGWIN__) || defined(_UWIN)
#include <sys/select.h>

#elif	defined(__hpux)
#include <time.h>

#elif	defined(__osf__)
#include <sys/time.h>
#include <sys/select.h>

#elif	defined(__sparc) || defined(__MVS__)
#include <sys/time.h>

#elif defined(__linux__) || defined(__APPLE__)
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#elif defined(VMS)
#include <time.h>
#include <socket.h>

#else
#error UNSUPPORTED PLATFORM

#endif
