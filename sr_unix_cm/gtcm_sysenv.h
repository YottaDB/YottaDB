/****************************************************************
 *								*
 *	Copyright 2001, 2009 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/*  gtcm_sysenv.h --- Include file specific to various systems. */

#ifndef GTCM_SYSENV_H
#define GTCM_SYSENV_H

#if !(defined(SUNOS) || defined(_AIX) || defined(__hpux) || defined(__linux__) || defined(__osf__) || defined(__MVS__)	\
		|| defined(__CYGWIN__) || defined (__APPLE__))
#	error	Unsupported Platform	/*  Unknown/unspecified system.  Should NOT compile */
#endif

/*  system interface */
#ifndef FILE_TCP
#	define BSD_TCP
#endif

#define BSD_LOG
#define BSD_MFD

#ifndef MDEF_included
#	error the below code assumes mdef.h has defined BIGENDIAN if appropriate
#endif

#ifdef BIGENDIAN
#	define BIG_END
#else
#	define LTL_END
#endif

/*  types */
typedef int	 omi_fd;

#define INV_FD_P(FD) ((FD) < 0)

/*  includes */
#include "gtm_socket.h"
#include "gtm_inet.h"
#include "gtm_netdb.h"
#include "gtm_syslog.h"
#include <sys/time.h>
#include <sys/types.h>

#ifdef BSD_TCP
#	define NET_TCP
#else
#	ifdef SYSV_TCP
#		define NET_TCP
#	endif
#endif

#endif /* !defined(GTCM_SYSENV_H) */
