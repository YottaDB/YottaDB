/****************************************************************
 *								*
 * Copyright (c) 2013-2018 Fidelity National Information	*
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
#ifndef GTMCRYPT_UTIL_H
#define GTMCRYPT_UTIL_H

#if !defined(DEBUG) && defined(assert)
# undef assert
# define assert(x)
#endif

#include "gtm_sizeof.h"			/* For SIZEOF. */
#include "gtm_limits.h"			/* For YDB_PATH_MAX. */
#include "gtm_common_defs.h"		/* To import common macros implemented by GT.M */
#include "ydb_logicals.h"		/* For ydbenvname and ydbenvindx_t */

#define	YDB_DIST_ENV			(ydbenvname[YDBENVINDX_DIST] + 1)		/* + 1 to skip leading $ */
#define	GTM_DIST_ENV			(gtmenvname[YDBENVINDX_DIST] + 1)		/* + 1 to skip leading $ */
#define USER_ENV			(ydbenvname[YDBENVINDX_GENERIC_USER] + 1)	/* + 1 to skip leading $ */

#define ENV_UNDEF_ERROR			"Environment variable %s not set"
#define ENV_UNDEF_ERROR2		"Environment variable %s/%s not set"
#define ENV_EMPTY_ERROR			"Environment variable %s set to empty string"
#define ENV_TOOLONG_ERROR		"Environment variable %s is too long (>= %d bytes)"

#define MAX_GTMCRYPT_STR_ARG_LEN	256
#define MAX_GTMCRYPT_ERR_STRLEN		2048

/* next two defines also in gtm_tls_interface.h and should be kept in sync */
#define GTM_PASSPHRASE_MAX		512	/* obfuscated */
#define GTM_PASSPHRASE_MAX_ASCII	(GTM_PASSPHRASE_MAX / 2)
#define PASSPHRASE_ENVNAME_MAX		64
#define GTMCRYPT_DEFAULT_PASSWD_PROMPT	"Enter Passphrase: "

#define GC_H2D(C, RES, MULT)							\
{										\
	if ((C >= 'A') && (C <= 'F'))						\
		RES = ((C - 'A') + 10) * MULT;					\
	else if ((C >= 'a') && (C <= 'f'))					\
		RES = ((C - 'a') + 10) * MULT;					\
	else if ((C >= '0') && (C <= '9'))					\
		RES = (C - '0') * MULT;						\
	else									\
		RES = -1;							\
}

/* Convert SOURCE, sequence of hexadecimal characters, into decimal representation. LEN denotes the length of the SOURCE string.
 * NOTE: Hexadecimal characters, presented in SOURCE string, has to be in upper case.
 */
#define GC_UNHEX(SOURCE, TARGET, LEN)						\
{										\
	int	i, res1, res2;							\
	char	c;								\
										\
	for (i = 0; i < LEN; i += 2)						\
	{									\
		c = SOURCE[i];							\
		GC_H2D(c, res1, 16);						\
		if (res1 < 0)							\
		{								\
			LEN = -1;						\
			TARGET[0] = (char)c;					\
			break;							\
		}								\
		c = SOURCE[i + 1];						\
		GC_H2D(c, res2, 1);						\
		if (res2 < 0)							\
		{								\
			LEN = -1;						\
			TARGET[0] = (char)c;					\
			break;							\
		}								\
		TARGET[i / 2] = (unsigned char)(res1 + res2);			\
	}									\
}

/* Convert SOURCE, sequence of decimal characters, into hexadecimal representation. LEN denotes the length of the TARGET string. */
#define GC_HEX(SOURCE, TARGET, LEN)						\
{										\
	int i;									\
										\
	for (i = 0; i < LEN; i += 2)						\
		SPRINTF(TARGET + i, "%02X", (unsigned char)SOURCE[i / 2]);	\
}

#define SNPRINTF(SRC, LEN, ...)							\
{										\
	int rc;									\
										\
	do									\
	{									\
		rc = snprintf(SRC, LEN, __VA_ARGS__);	/* BYPASSOK */		\
	} while ((-1 == rc) && (EINTR == errno)); /* EINTR-safe */		\
}

#define SPRINTF(SRC, ...)							\
{										\
	int rc;									\
										\
	do									\
	{									\
		rc = sprintf(SRC, __VA_ARGS__);	/* BYPASSOK */			\
	} while ((-1 == rc) && (EINTR == errno)); /* EINTR-safe */		\
}

/* Some helper error reporting macros. */
#define UPDATE_ERROR_STRING(...)						\
{										\
	SNPRINTF(gtmcrypt_err_string, MAX_GTMCRYPT_ERR_STRLEN, __VA_ARGS__);	\
}

#define STR_QUOT(X)			#X
#define STR_WRAP(X)			STR_QUOT(X)
#define STR_ARG				"%." STR_WRAP(MAX_GTMCRYPT_STR_ARG_LEN) "s%s"
#define ELLIPSIZE(STR)			STR, (strlen(STR) > MAX_GTMCRYPT_STR_ARG_LEN ? "..." : "")

#define IS_FIPS_MODE_REQUESTED(RV)									\
{													\
	char *ptr;											\
													\
	RV = FALSE;											\
	if (NULL != (ptr = ydb_getenv(YDBENVINDX_CRYPT_FIPS, NULL_SUFFIX, NULL_IS_YDB_ENV_MATCH)))	\
	{												\
		if ((0 == strcasecmp(ptr, "YES"))							\
			|| (0 == strcasecmp(ptr, "TRUE"))						\
			|| (*ptr == '1')								\
			|| (*ptr == 'Y')								\
			|| (*ptr == 'y'))								\
		{											\
			RV = TRUE;									\
		}											\
	}												\
}

/* In OpenSSL versions prior to 1.0.1, "FIPS_mode_set", the function that enables/disables FIPS mode, is available only
 * in a FIPS capable OpenSSL (that is, OpenSSL built from source with "fips" option). From 1.0.1, the function was moved
 * to "crypto/o_fips.c" and made as a wrapper to the actual FIPS Object Module and so was available on OpenSSL versions
 * built with or without the "fips" flag. Since the plugin would be run in an environment having OpenSSL with or without
 * FIPS capability, we first do a dlopen with a special NULL parameter to get access to the global namespace and then
 * do a dlsym to see if the "FIPS_mode_set" function is available. If not (or "FIPS_mode_set" returns 0), we raise an error.
 */
#define ENABLE_FIPS_MODE(RV, FIPS_ENABLED)										\
{															\
	void	*handle;												\
	int	(*FIPS_mode_set_fnptr)(int);										\
															\
	handle = dlopen(NULL, (RTLD_NOW | RTLD_GLOBAL));								\
	RV = 0;														\
	FIPS_ENABLED = FALSE;												\
	if (NULL == handle)												\
	{														\
		UPDATE_ERROR_STRING("Failed to dlopen the global namespace. Reason: %s.", dlerror());			\
		assert(FALSE);												\
		RV = -1;												\
	} else														\
	{														\
		FIPS_mode_set_fnptr = (int(*)(int))dlsym(handle, "FIPS_mode_set");					\
		if (NULL != FIPS_mode_set_fnptr)									\
		{	/* Symbol available. Invoke it to enable FIPS mode. */						\
			if (FIPS_mode_set_fnptr(1))									\
				FIPS_ENABLED = TRUE;									\
			else												\
			{												\
				GC_APPEND_OPENSSL_ERROR("Failed to initialize FIPS mode.");				\
				RV = -1;										\
			}												\
		} else													\
		{													\
			UPDATE_ERROR_STRING("Failed to initialize FIPS mode. Reason: cannot find OpenSSL FIPS "		\
						"functions in the runtime system.");					\
			RV = -1;											\
		}													\
	}														\
}

/* OpenSSL specific error handling. */
#define GC_APPEND_OPENSSL_ERROR(...)											\
{															\
	char	*errptr, *end;												\
	int	rv;													\
															\
	errptr = &gtmcrypt_err_string[0];										\
	end = errptr + MAX_GTMCRYPT_ERR_STRLEN;										\
	SNPRINTF(errptr, MAX_GTMCRYPT_ERR_STRLEN, __VA_ARGS__);								\
	errptr += STRLEN(errptr);											\
	SNPRINTF(errptr, end - errptr, "%s", " Reason: ");								\
	errptr += STRLEN(errptr);											\
	rv = ERR_get_error();												\
	ERR_error_string_n(rv, errptr, end - errptr);									\
}

/* Libgcrypt specific error handling. */
#define GC_APPEND_GCRY_ERROR(ERR, ...)											\
{															\
	char *errptr, *end;												\
															\
	errptr = &gtmcrypt_err_string[0];										\
	end = errptr + MAX_GTMCRYPT_ERR_STRLEN;										\
	SNPRINTF(errptr, MAX_GTMCRYPT_ERR_STRLEN, __VA_ARGS__);								\
	errptr += STRLEN(errptr);											\
	SNPRINTF(errptr, end - errptr, "%s", " Reason: ");								\
	errptr += STRLEN(errptr);											\
	SNPRINTF(errptr, end - errptr, "%s", gcry_strerror(ERR));							\
}

/* CYGWIN TODO: This is to fix a linker error. Undo when it is fixed. */
#if !defined(USE_SYSLIB_FUNCS) && !defined(__CYGWIN__)
#define	MALLOC			(*gtm_malloc_fnptr)
#define FREE			(*gtm_free_fnptr)
#else
#define MALLOC			malloc
#define FREE			free
#endif

typedef struct
{
	ydbenvindx_t		envindx;	/* Index into ydbenvname[] array pointing to environment variable name */
	int			passwd_len;	/* Length of the password that's allocated. */
	char			suffix[PASSPHRASE_ENVNAME_MAX];	/* Suffix to add to env var name ("envindx") if needed */
	char			*env_value;	/* Obfuscated version of the password stored in the environment (as hex) */
	char			*passwd;	/* Password in clear text. */
} passwd_entry_t;

typedef void *			(*gtm_malloc_fnptr_t)(size_t);
typedef void			(*gtm_free_fnptr_t)(void *);

GBLREF gtm_malloc_fnptr_t	gtm_malloc_fnptr;
GBLREF gtm_free_fnptr_t		gtm_free_fnptr;

GBLREF	char			gtmcrypt_err_string[MAX_GTMCRYPT_ERR_STRLEN];

int				gc_load_yottadb_symbols(void);
void 				gtm_gcry_log_handler(void *opaque, int level, const char *fmt, va_list arg_ptr);
int				gc_read_passwd(char *prompt, char *buf, int maxlen, void *tty);
int				gc_mask_unmask_passwd(int nparm, gtm_string_t *in, gtm_string_t *out);
void				gc_freeup_pwent(passwd_entry_t *pwent);
int 				gc_update_passwd(ydbenvindx_t envindx, char *suffix, passwd_entry_t **ppwent,
											char *prompt, int interactive);
#endif
