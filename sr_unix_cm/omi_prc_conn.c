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

/*
 *  omi_prc_conn.c ---
 *
 *	Process a connection request.
 *
 */

#ifndef lint
static char rcsid[] = "$Header:$";
#endif

#include "mdef.h"

#include "gtm_string.h"
#include "gtm_stdio.h"
#if !defined(__APPLE__) && !defined(__MVS__)
#include <crypt.h> /* for crypt(), actually it is in unistd.h */
#endif
#include "gtm_unistd.h" /* for crypt() */

#include "gtcm.h"
#include "omi.h"
#include "gtm_pwd.h"
#include "gtm_ctype.h"
#include "gtmio.h"
#include "have_crit.h"
#ifdef GTCM_RC
#include "rc.h"
#endif /* defined(GTCM_RC) */
#include "release_name.h"

#ifdef SHADOWPW
#include <shadow.h>

#include "gtm_stat.h"

#include <errno.h>
#endif

#define OMI_FREE 		\
{				\
	if (NULL != ag_name)	\
		free(ag_name);	\
	if (NULL != ag_pass)	\
		free(ag_pass);	\
}

#undef MIN
#define MIN(A, B) (((A) < (B)) ? (A) : (B))

GBLREF int	authenticate;

int omi_prc_conn(omi_conn *cptr, char *xend, char *buff, char *bend)
{
    omi_si	si, eightbit, chartran, ss_len, ext_cnt;
    omi_li	li_min, li_max, ext;
    uns_short	li_val;
    int		len, i;
    char	*bptr, *eptr;
    char	*ag_name, *ag_pass, *s;

    ASSERT_IS_LIBGTCM;
    bptr = buff;
    ag_name = ag_pass = NULL;

/*  Version numbers */
    OMI_SI_READ(&si, cptr->xptr);
    if (si.value != OMI_PROTO_MAJOR)
	return -OMI_ER_SE_VRSNOTSUPP;
    OMI_SI_WRIT(OMI_PROTO_MAJOR, bptr);
/*  XXX minor version numbers */
    OMI_SI_READ(&si, cptr->xptr);
    OMI_SI_WRIT(OMI_PROTO_MINOR, bptr);

/*  Minimum and maximum parameters */

/*  Data */
    OMI_LI_READ(&li_min, cptr->xptr);
    if (OMI_MAX_DATA < li_min.value)
    {
	cptr->state = OMI_ST_CLOS;
	return -OMI_ER_SE_LENMIN;
    }
    OMI_LI_READ(&li_max, cptr->xptr);
    li_val = MIN(li_max.value, OMI_MAX_DATA);
    OMI_LI_WRIT(li_val, bptr);

/*  Subscript */
    OMI_LI_READ(&li_min, cptr->xptr);
    if (OMI_MAX_SUBSCR < li_min.value)
    {
	cptr->state = OMI_ST_CLOS;
	return -OMI_ER_SE_LENMIN;
    }
    OMI_LI_READ(&li_max, cptr->xptr);
    li_val = MIN(li_max.value, OMI_MAX_SUBSCR);
    OMI_LI_WRIT(li_val, bptr);

/*  Reference */
    OMI_LI_READ(&li_min, cptr->xptr);
    if (OMI_MAX_REF < li_min.value)
    {
	cptr->state = OMI_ST_CLOS;
	return -OMI_ER_SE_LENMIN;
    }
    OMI_LI_READ(&li_max, cptr->xptr);
    li_val = MIN(li_max.value, OMI_MAX_REF);
    OMI_LI_WRIT(li_val, bptr);

/*  Message */
    OMI_LI_READ(&li_min, cptr->xptr);
    if (cptr->bsiz < li_min.value)
    {
	cptr->state = OMI_ST_CLOS;
	return -OMI_ER_SE_LENMIN;
    }
    OMI_LI_READ(&li_max, cptr->xptr);
    li_val = MIN(li_max.value, cptr->bsiz);
    OMI_LI_WRIT(li_val, bptr);

/*  Oustanding */
    OMI_LI_READ(&li_min, cptr->xptr);
    if (1 < li_min.value)
    {
	cptr->state = OMI_ST_CLOS;
	return -OMI_ER_SE_LENMIN;
    }
    OMI_LI_READ(&li_max, cptr->xptr);
    li_val = MIN(li_max.value, 1);
    OMI_LI_WRIT(li_val, bptr);

/*  Other parameters */
    OMI_SI_READ(&eightbit, cptr->xptr);
    OMI_SI_WRIT(eightbit.value, bptr);
    OMI_SI_READ(&chartran, cptr->xptr);
    OMI_SI_WRIT(chartran.value, bptr);

/*  Bounds checking */
    if (cptr->xptr > xend || bptr >= bend)
	return -OMI_ER_PR_INVMSGFMT;

/*  Implementation ID (in) */
    OMI_SI_READ(&ss_len,   cptr->xptr);
    cptr->xptr += ss_len.value;

/*  Agent name (in) */
    OMI_SI_READ(&ss_len, cptr->xptr);
    ag_name = (char *)malloc(ss_len.value + 1);
    assert(ss_len.value < MAX_USER_NAME && ss_len.value > 0);
    memcpy(ag_name, cptr->xptr, ss_len.value);
    ag_name[ss_len.value] = '\0';
    strcpy(cptr->ag_name, ag_name);
    cptr->xptr += ss_len.value;

/*  Agent password (in) */
    OMI_SI_READ(&ss_len, cptr->xptr);
    ag_pass = (char *)malloc(ss_len.value + 1);
    memcpy(ag_pass, cptr->xptr, ss_len.value);
    ag_pass[ss_len.value] = '\0';
    cptr->xptr += ss_len.value;

/* No support for authentication on SCO, Linux, Cygwin, or z/OS at the moment...*/
#if !(defined(SCO) || defined(__linux__) || defined(__CYGWIN__) || defined(__MVS__))
    if (authenticate)  /* verify password and user name */
    {
#	ifdef SHADOWPW
	struct spwd *spass, *getspnam();
	struct stat buf;
#	endif
	struct passwd *pass;
	char *pw, *syspw;

	/* lowercase agent name */
	for(s = ag_name; *s; s++)
		*s = TOLOWER(*s);

#ifdef SHADOWPW
	if (!Stat("/etc/shadow", &buf))
	{
		if ((spass = getspnam(ag_name)) == NULL)
		{
		    if (errno)
		    {
			OMI_DBG((omi_debug, "%s:  error opening /etc/shadow for input\n",
				 SRVR_NAME, ag_name));
			OMI_FREE;
			PERROR("/etc/shadow");
			return -OMI_ER_DB_USERNOAUTH;
		    }
		    OMI_DBG((omi_debug, "%s:  user %s not found in /etc/shadow\n",
			     SRVR_NAME, ag_name));
		    OMI_FREE;
		    return -OMI_ER_DB_USERNOAUTH;
		}
		syspw = spass->sp_pwdp;
	} else if ((pass = getpwnam(ag_name)) == NULL)
	{
		OMI_DBG((omi_debug, "%s:  user %s not found in /etc/passwd\n",
			SRVR_NAME, ag_name));
		OMI_FREE;
		return -OMI_ER_DB_USERNOAUTH;
	} else
		syspw = pass->pw_passwd;
#	else    /* ndef SHADOWPW */
	if ((pass = getpwnam(ag_name)) == NULL)
	{
		    OMI_DBG((omi_debug, "%s:  user %s not found in /etc/passwd\n",
			     SRVR_NAME, ag_name));
		    OMI_FREE;
		    return -OMI_ER_DB_USERNOAUTH;
	    } else
		syspw = pass->pw_passwd;
#	endif   /* SHADOWPW */
	pw = (char *)crypt(ag_pass, syspw);
	if (strcmp(pw, syspw) != 0)
	{
		OMI_DBG((omi_debug, "%s:  login attempt for user %s failed.\n",
			SRVR_NAME, ag_name));
		OMI_FREE;
		return -OMI_ER_DB_USERNOAUTH;
	}
    }
#endif  /* SCO or linux or cygwin or z/OS */

/*  Server name (in) */
    OMI_SI_READ(&ss_len,   cptr->xptr);
    cptr->xptr += ss_len.value;

/*  Implementation ID (out) */
    len = SIZEOF(YDB_RELEASE_NAME) - 1;
    OMI_SI_WRIT(len, bptr);
    (void) memcpy(bptr, YDB_RELEASE_NAME, len);
    bptr += len;
/*  Server name (out) */
    OMI_SI_WRIT(0, bptr);
/*  Server password (out) */
    OMI_SI_WRIT(0, bptr);
/*  Bounds checking */
    if (cptr->xptr > xend || bptr >= bend)
    {
	OMI_FREE;
	return -OMI_ER_PR_INVMSGFMT;
    }
/*  Extensions (in) -- count through them */
    OMI_SI_READ(&ext_cnt, cptr->xptr);
    for (i = 0; i < ext_cnt.value; i++)
    {
	OMI_LI_READ(&ext, cptr->xptr);
	cptr->exts |= (1 << (ext.value - 1));
    }
/*  Mask off extensions we don't support */
    cptr->exts &= OMI_EXTENSIONS;

/*  Negotiate extension combinations */
    if (cptr->exts & OMI_XTF_RC && cptr->exts & OMI_XTF_BUNCH)
	cptr->exts &= ~OMI_XTF_BUNCH;
#   ifdef GTCM_RC
    if (cptr->exts & OMI_XTF_RC)
	cptr->of = rc_oflow_alc();
#   endif /* defined(GTCM_RC) */

/*  Extensions (out) */
    eptr  = bptr;
    bptr += OMI_SI_SIZ;
    i     = 0;
    if (cptr->exts & OMI_XTF_BUNCH)
    {
	OMI_LI_WRIT(OMI_XTN_BUNCH, bptr);
	i++;
    }
    if (cptr->exts & OMI_XTF_GGR)
    {
	OMI_LI_WRIT(OMI_XTN_GGR, bptr);
	i++;
    }
    if (cptr->exts & OMI_XTF_NEWOP)
    {
	OMI_LI_WRIT(OMI_XTN_NEWOP, bptr);
	i++;
    }
    if (cptr->exts & OMI_XTF_RC)
    {
	OMI_LI_WRIT(OMI_XTN_RC, bptr);
	i++;
    }
/*  Number of extensions */
    OMI_SI_WRIT(i, eptr);
/*  Bounds checking */
    if (cptr->xptr > xend || bptr >= bend)
    {
	OMI_FREE;
	return -OMI_ER_PR_INVMSGFMT;
    }
/*  Change the state of the connection */
    cptr->state = OMI_ST_CONN;
    OMI_FREE;
    return (int)(bptr - buff);
}
