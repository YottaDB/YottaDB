/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.*
 * All rights reserved.						*
 *								*
 * Copyright (c) 2017 Stephen L Johnson. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* If this is not the vax, define string.h. This is because the Vax
   has its own built-in instructions for string manipulation.
*/
#ifndef GTM_STRINGH
#define GTM_STRINGH

#include <string.h>

#define STRERROR	strerror
#define	STRCPY(DEST, SOURCE)		strcpy((char *)(DEST), (char *)(SOURCE))
#define	STRNCPY_STR(DEST, STRING, LEN)	strncpy((char *)(DEST), (char *)(STRING), LEN)
#define	STRCMP(SOURCE, DEST)		strcmp((char *)(SOURCE), (char *)(DEST))
#define	STRNCMP_LIT(SOURCE, LITERAL)	strncmp(SOURCE, LITERAL, SIZEOF(LITERAL) - 1)		/* BYPASSOK */
/* Make sure that SIZEOF(SOURCE) > 0 or SOURCE != NULL before running. */
#define	STRNCMP_LIT_FULL(SOURCE, LITERAL)	strncmp(SOURCE, LITERAL, SIZEOF(LITERAL))	/* BYPASSOK */
#define STRNCMP_LIT_LEN(SOURCE, LITERAL, LENGTH)	\
		strncmp((const char *)(SOURCE), (const char *)(LITERAL), MIN(SIZEOF(LITERAL), LENGTH))	/* BYPASSOK */
#define	STRNCMP_STR(SOURCE, STRING, LEN) strncmp(SOURCE, STRING, LEN)
/* Ensure that our uses of STRTOK and STRTOK_R are not called inside a timer handler */
#define STRTOK_R(STR, DELIM, SAVE)	(DBG_ASSERT(FALSE == timer_in_handler) strtok_r(STR, DELIM, SAVE))
/* We need to catch any memcpy() that is used when the source and target strings overlap in any fashion so we can change
 * them to a memmove. So in debug builds, assert fail if this is the case.
 */
#if defined(DEBUG) && !defined(BYPASS_MEMCPY_OVERRIDE)
#  include "gtm_memcpy_validate_and_execute.h"
#  ifdef memcpy
#    undef memcpy	/* Some platforms like AIX create memcpy as a #define which needs removing before re-define */
#  endif
#  define memcpy(TARGET, SRC, LEN) gtm_memcpy_validate_and_execute((void *)(TARGET), (const void *)(SRC), (LEN))
#endif
/* The strnlen() function is POSIX-2008 so not widely avaiable yet so use it or our own critter as appropriate */
#if (defined(__linux__) || defined(AIX)) || defined(__APPLE__)
# define STRNLEN(str, maxlen, rslt) rslt = strnlen(str, maxlen)
#else
# define STRNLEN(str, maxlen, rslt)						\
{										\
	unsigned char	*c, *cend;						\
										\
	for (c = (unsigned char *)str, cend = c + maxlen; c < cend; ++c)	\
		if ('\0' == *c)							\
			break;							\
	rslt = c - (unsigned char *)str;					\
}
#endif
#define STRNDUP(STR, MAXLEN, DST)						\
{										\
	size_t local_len;							\
										\
	STRNLEN(STR, MAXLEN, local_len);					\
	DST = (char *) malloc(local_len + 1);					\
	memcpy(DST, STR, local_len);						\
	DST[local_len] = '\0';							\
}

#endif
