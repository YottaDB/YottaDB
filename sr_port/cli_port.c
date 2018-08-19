/****************************************************************
 *								*
 * Copyright (c) 2001-2017 Fidelity National Information	*
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

#include "mdef.h"

#include "gtm_ctype.h"
#include "gtm_stdio.h"
#include "gtm_stdlib.h"
#include "gtm_string.h"

#include "gtm_limits.h"
#include <errno.h>

#include "cli.h"
#include "util.h"

/* A lot of stuff that can be made portable across unix and vvms cli.c needs to be moved into this module.
 * For a start, cli_str_to_hex() is moved in. At least cli_get_str(), cli_get_int(), cli_get_num() can be moved in later.
 */

/*
*-------------------------------------------------------
* Check if string is a decimal number
*
* Return:
*	TRUE    - only decimal digits
*	FALSE   - otherwise
* -------------------------------------------------------
*/
int cli_is_dcm(char *p)
{
        while (*p && ISDIGIT_ASCII(*p))
                p++;

        if (*p) return (FALSE);
        else return (TRUE);
}

/*
 * --------------------------------------------------
 * Convert string to hex.
 *
 * Return:
 *	TRUE	- OK
 *	FALSE	- Could not convert to hex
 * --------------------------------------------------
 */
boolean_t cli_str_to_hex(char *str, uint4 *dst)
{
	unsigned long	result;
	int	save_errno;

	save_errno = errno;
	errno = 0;
        result = STRTOUL(str, NULL, 16);
	if (
#if INT_MAX < LONG_MAX
		(UINT_MAX < result) ||	/* outside UINT range */
#endif
	    (ERANGE == errno && ULONG_MAX == result) || (0 == result && 0 != errno))
	{	/* out of range or other error */
		*dst = 0;
		errno = save_errno;
		return FALSE;
	} else
	{
		*dst = (uint4)result;
		errno = save_errno;
		return TRUE;
	}
}

/*
 * --------------------------------------------------
 * Convert string to 64 bit hex.
 *
 * Return:
 *	TRUE	- OK
 *	FALSE	- Could not convert to hex
 * --------------------------------------------------
 */
boolean_t cli_str_to_hex64(char *str, gtm_uint64_t *dst)
{
	gtm_uint64_t	result;
	int		save_errno;

	save_errno = errno;
	errno = 0;
        result = STRTOU64L(str, NULL, 16);
	if ((ERANGE == errno && GTM_UINT64_MAX == result) || (0 == result && 0 != errno))
	{	/* out of range or other error */
		*dst = 0;
		errno = save_errno;
		return FALSE;
	} else
	{
		*dst = result;
		errno = save_errno;
		return TRUE;
	}
}

/*
 * --------------------------------------------------
 * Convert string to 64 bit unsigned int.
 *
 * Return:
 *	TRUE	- OK
 *	FALSE	- Could not convert to int
 * --------------------------------------------------
 */
boolean_t cli_str_to_uint64(char *str, gtm_uint64_t *dst)
{
	gtm_uint64_t	result;
	int		save_errno;

	save_errno = errno;
	errno = 0;
        result = STRTOU64L(str, NULL, 10);
	if ((ERANGE == errno && GTM_UINT64_MAX == result) || (0 == result && 0 != errno))
	{	/* out of range or other error */
		*dst = 0;
		errno = save_errno;
		return FALSE;
	} else
	{
		*dst = result;
		errno = save_errno;
		return TRUE;
	}
}

/*
 * --------------------------------------------------
 * Convert string to int.
 *
 * Return:
 *	TRUE	- OK
 *	FALSE	- Could not convert to int
 * --------------------------------------------------
 */
boolean_t cli_str_to_int(char *str, int4 *dst)
{
	long	result;
	int	save_errno;

	save_errno = errno;
	errno = 0;
        result = STRTOL(str, NULL, 10);
	if (
#if INT_MAX < LONG_MAX
		(INT_MIN > result || INT_MAX < result) ||	/* outside INT range */
#endif
	    (ERANGE == errno && (LONG_MIN == result || LONG_MAX == result)) || (0 == result && 0 != errno))
	{	/* out of range or other error */
		*dst = 0;
		errno = save_errno;
		return FALSE;
	} else
	{
		*dst = (int4)result;
		errno = save_errno;
		return TRUE;
	}
}

/*
 * --------------------------------------------------
 * Convert string to 64 bit int.
 *
 * Return:
 *	TRUE	- OK
 *	FALSE	- Could not convert to int
 * --------------------------------------------------
 */
boolean_t cli_str_to_int64(char *str, gtm_int64_t *dst)
{
	gtm_int64_t	result;
	int		save_errno;

	save_errno = errno;
	errno = 0;
        result = STRTO64L(str, NULL, 10);
	if ((ERANGE == errno && (GTM_INT64_MIN == result || GTM_INT64_MAX == result)) || (0 == result && 0 != errno))
	{	/* out of range or other error */
		*dst = 0;
		errno = save_errno;
		return FALSE;
	} else
	{
		*dst = result;
		errno = save_errno;
		return TRUE;
	}
}

/*
 * --------------------------------------------------
 * Convert string to number.
 *
 * Return:
 *	TRUE	- OK
 *	FALSE	- Could not convert to number
 * --------------------------------------------------
 */
boolean_t cli_str_to_num(char *str, int4 *dst)
{
	long	result;
	int	save_errno, base;

	save_errno = errno;
	errno = 0;
	if (cli_is_dcm(str))
		base = 10;
	else
		base = 16;
        result = STRTOL(str, NULL, base);
	if (
#if INT_MAX < LONG_MAX
		(INT_MIN > result || INT_MAX < result) ||	/* outside INT range */
#endif
	    (ERANGE == errno && (LONG_MIN == result || LONG_MAX == result)) || (0 == result && 0 != errno))
	{	/* out of range or other error */
		*dst = 0;
		errno = save_errno;
		return FALSE;
	} else
	{
		*dst = (int4)result;
		errno = save_errno;
		return TRUE;
	}
}

/*
 * --------------------------------------------------
 * Convert string to 64 bit number.
 *
 * Return:
 *	TRUE	- OK
 *	FALSE	- Could not convert to number
 * --------------------------------------------------
 */
boolean_t cli_str_to_num64(char *str, gtm_int64_t *dst)
{
	gtm_int64_t	result;
	int	save_errno, base;

	save_errno = errno;
	errno = 0;
	if (cli_is_dcm(str))
		base = 10;
	else
		base = 16;
        result = STRTO64L(str, NULL, base);
	if ((ERANGE == errno && (GTM_INT64_MIN == result || GTM_INT64_MAX == result)) || (0 == result && 0 != errno))
	{	/* out of range or other error */
		*dst = 0;
		errno = save_errno;
		return FALSE;
	} else
	{
		*dst = result;
		errno = save_errno;
		return TRUE;
	}
}

int cli_parse_two_numbers(char *qual_name, const char delimiter, uint4 *first_num, uint4 *second_num)
{ /* Parse two unsigned base 10 numbers separated by the given delimiter. Eg. -LOG_INTERVAL=10,20 (on VMS, -LOG_INTERVAL="10,20").
   * Both Unix and VMS accept the qualifier as a string. NOTE: On VMS, such qualifiers are quoted strings.
   * Both numbers are optional (eg. -LOG_INTERVAL=10, or -LOG_INTERVAL=",20", or -LOG_INTERVAL=,).
   * Return values:
   * 		     CLI_2NUM_FIRST_SPECIFIED  (binary 10), first number specified, second not
   * 		     CLI_2NUM_SECOND_SPECIFIED (binary 01), first number not specified, second is
   * 		     CLI_2NUM_BOTH_SPECIFIED   (binary 11) (CLI_2NUM_FIRST_SPECIFIED | CLI_2NUM_SECOND_SPECIFIED), both specified
   * 		     0 (binary 00), error in parsing either number
   */
	char		*first_num_str, *second_num_str, *two_num_str_top, *num_endptr;
	char		two_num_qual_str[128];
	unsigned short	two_num_qual_len;
	uint4		num, prev_value;
	int		retval = 0;
	char		*num_ch;

	two_num_qual_len = SIZEOF(two_num_qual_str);
	if (!cli_get_str(qual_name, two_num_qual_str, &two_num_qual_len))
	{
		util_out_print("Error parsing !AZ qualifier", TRUE, qual_name);
		return 0;
	}
	first_num_str = two_num_qual_str;
	for (second_num_str = first_num_str, two_num_str_top = first_num_str + two_num_qual_len;
		second_num_str < two_num_str_top && delimiter != *second_num_str;
		second_num_str++)
		;
	if (delimiter == *second_num_str)
		*second_num_str++ = '\0';
	if (*first_num_str != '\0') /* VMS issues EINVAL if strtoul is passed null string */
	{
		num = 0;
		prev_value = 0;
		for (num_ch = first_num_str; ((*num_ch >= '0' && *num_ch <= '9') && (*num_ch != '\0')); num_ch++)
		{
			num = num * 10 + (*num_ch - '0');
			if (num < prev_value)
				break;
			prev_value = num;
		}
		if ((num > INT_MAX) || ('\0' != *num_ch))
		{
			util_out_print("Error parsing or invalid parameter for !AZ", TRUE, qual_name);
			return 0;
		}
		*first_num = num;
		retval |= CLI_2NUM_FIRST_SPECIFIED;
	} /* else, first number not specified */
	if (second_num_str < two_num_str_top && *second_num_str != '\0')
	{
		num = 0;
		prev_value = 0;
		for (num_ch = second_num_str; ((*num_ch >= '0' && *num_ch <= '9') && (*num_ch != '\0')); num_ch++)
		{
			num = num * 10 + (*num_ch - '0');
			if (num < prev_value)
				break;
			prev_value = num;
		}
		if ((num > INT_MAX) || ('\0' != *num_ch))
		{
			util_out_print("Error parsing or invalid parameter for LOG_INTERVAL", TRUE);
			return 0;
		}
		*second_num = num;
		retval |= CLI_2NUM_SECOND_SPECIFIED;
	} /* else, second number not specified */
	return retval;
}
