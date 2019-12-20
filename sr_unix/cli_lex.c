/****************************************************************
 *								*
 * Copyright (c) 2001-2019 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2018-2020 YottaDB LLC and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/*
 * -----------------------------------------------------
 * Lexical analyzer routines for command line interpreter
 * -----------------------------------------------------
 */

#include "mdef.h"

#include "gtm_ctype.h"
#include <errno.h>
#include "gtm_stdio.h"
#include "gtm_string.h"
#ifdef UTF8_SUPPORTED
#include "gtm_icu_api.h"
#include "gtm_utf8.h"
#endif

#include "cli.h"
#include "eintr_wrappers.h"
#include "min_max.h"

GBLDEF char	cli_token_buf[MAX_LINE + 1];	/* Token buffer */
GBLREF int	cmd_cnt;
GBLREF char	**cmd_arg;
GBLDEF boolean_t gtm_cli_interpret_string = TRUE;
GBLDEF IN_PARMS *cli_lex_in_ptr;

#ifdef UTF8_SUPPORTED
GBLREF	boolean_t	gtm_utf8_mode;
#define CLI_GET_CHAR(PTR, BUFEND, CHAR) (gtm_utf8_mode ? UTF8_MBTOWC(PTR, BUFEND, CHAR) : (CHAR = (wint_t)*(PTR), (PTR) + 1))
#define CLI_PUT_CHAR(PTR, CHAR) (gtm_utf8_mode ? UTF8_WCTOMB(CHAR, PTR) : (*(PTR) = CHAR, (PTR) + 1))
#define CLI_ISSPACE(CHAR) (gtm_utf8_mode ? U_ISSPACE(CHAR) : ISSPACE_ASCII((int)CHAR))
#else
#define CLI_GET_CHAR(PTR, BUFEND, CHAR) (CHAR = (int)*(PTR), (PTR) + 1)
#define CLI_PUT_CHAR(PTR, CHAR) (*(PTR) = CHAR, (PTR) + 1)
#define CLI_ISSPACE(CHAR) ISSPACE_ASCII(CHAR)
#endif

static int tok_string_extract(void)
{
	int		token_len;
	boolean_t	have_quote, first_quote;
	uchar_ptr_t	in_sp, out_sp, in_next, last_in_next,
			bufend;	/* really one past last byte of buffer */
#	ifdef UTF8_SUPPORTED
	wint_t		ch;
#	else
	int		ch;
#	endif

	assert(cli_lex_in_ptr);
	in_sp = (uchar_ptr_t)cli_lex_in_ptr->tp;
	bufend = (uchar_ptr_t)&cli_lex_in_ptr->in_str[0] + cli_lex_in_ptr->buflen;
	out_sp = (uchar_ptr_t)cli_token_buf;
	token_len = 0;
	have_quote = FALSE;
	last_in_next = in_next = CLI_GET_CHAR(in_sp, bufend, ch);
	for ( ; ;)
	{
		/* '-' is not a token separator */
		while (ch && !CLI_ISSPACE(ch))
		{
			last_in_next = in_next;
			if (ch == '"')
			{
				if (!have_quote)
				{
					if (!gtm_cli_interpret_string)
					{
						out_sp = CLI_PUT_CHAR(out_sp, ch);
						token_len++;
					}
					have_quote = TRUE;
					in_next = CLI_GET_CHAR(in_next, bufend, ch);
				} else
				{
					if (!gtm_cli_interpret_string)
					{
						out_sp = CLI_PUT_CHAR(out_sp, ch);
						token_len++;
					}
					in_next = CLI_GET_CHAR(in_next, bufend, ch);
					if (ch == '"')
					{ /* double quote, one goes in string, still have quote */
						out_sp = CLI_PUT_CHAR(out_sp, ch);
						in_next = CLI_GET_CHAR(in_next, bufend, ch);
						token_len++;
					} else
						have_quote = FALSE;
				}
			} else
			{
				out_sp = CLI_PUT_CHAR(out_sp, ch);
				in_next = CLI_GET_CHAR(in_next, bufend, ch);
				token_len++;
			}
		}
		if (ch == '\0')
		{
			in_sp = last_in_next;	/* Points to start of null char so scan ends next call */
			break;
		}
		if (have_quote)
		{
			out_sp = CLI_PUT_CHAR(out_sp, ch);
			in_next = CLI_GET_CHAR(in_next, bufend, ch);
			token_len++;
			continue;
		}
		in_sp = in_next;
		break;
	}
	ch = 0;
	out_sp = CLI_PUT_CHAR(out_sp, ch);
	cli_lex_in_ptr->tp = (char *)in_sp;

	return (token_len);
}


/*
 * -------------------------
 * Inintialize lexer
 * -------------------------
 */
#ifdef __osf__
/* N.B. If the process is started by mumps, argv passed in from main (in gtm.c) is almost straight from the operating system.
 * if the process is started externally (call-ins), argc and argv are 0 and NULL respectively */
#pragma pointer_size (save)
#pragma pointer_size (long)
#endif
void	cli_lex_setup (int argc, char **argv)
{
	int	parmlen, parmindx;
	char	**parmptr;
#	ifdef __osf__
#	pragma pointer_size (restore)
#	endif
#	ifdef KEEP_zOS_EBCDIC
	__argvtoascii_a(argc, argv);
#	endif
	cmd_cnt = argc;
	cmd_arg = (char **)argv;
	/* Quickly run through the parameters to get a ballpark on the
	   size of the string needed to store them.
	*/
	for (parmindx = 0, parmptr = argv, parmlen = PARM_OVHD; parmindx < argc; parmptr++, parmindx++)
	{
		if (MAX_LINE < (parmlen + STRLEN(*parmptr) + 1))
		{	/* Copy as much as possible from the command string */
			parmlen = MAX_LINE;
			break;
		}
		parmlen += STRLEN(*parmptr) + 1;
	}
	/* call-ins may repeatedly initialize cli_lex_setup for every invocation of gtm_init() */
	if (!cli_lex_in_ptr || parmlen > cli_lex_in_ptr->buflen)
	{	/* We have the cure for a missing or unusable buffer */
		if (cli_lex_in_ptr)
			free(cli_lex_in_ptr);
		cli_lex_in_ptr = (IN_PARMS *)malloc(SIZEOF(IN_PARMS) + parmlen + 1);	/* + 1 needed for NULL byte */
		cli_lex_in_ptr->buflen = parmlen;
	}
	cli_lex_in_ptr->argc = argc;
	cli_lex_in_ptr->argv = argv;
	cli_lex_in_ptr->in_str[0] = '\0';
	cli_lex_in_ptr->tp = NULL;
}

void cli_str_setup(uint4 addrlen, char *addr)
{	/* callers trigger_parse and zl_cmd_qlf create command strings with knowledge of their length */
	uint4	alloclen;

	assert(cli_lex_in_ptr);
	alloclen = ((MAX_LINE <= addrlen) ? MAX_LINE : addrlen) + 1;
	if (!cli_lex_in_ptr || alloclen > cli_lex_in_ptr->buflen)
	{	/* We have the cure for a missing or unusable buffer */
		if (cli_lex_in_ptr)
			free(cli_lex_in_ptr);
		cli_lex_in_ptr = (IN_PARMS *)malloc(SIZEOF(IN_PARMS) + alloclen + 1);	/* + 1 ensures room for <NUL> terminator */
		cli_lex_in_ptr->buflen = alloclen;
	}
	cli_lex_in_ptr->argv = NULL;
	cli_lex_in_ptr->argc = 0;
	cli_lex_in_ptr->tp = cli_lex_in_ptr->in_str;
	addrlen = MIN(addrlen, alloclen - 1);
	memcpy(cli_lex_in_ptr->in_str, addr, addrlen);
	(cli_lex_in_ptr->in_str)[addrlen] = '\0';
}

/*
 * ---------------------------------------------------------------
 * Convert string to upper case. Do it only for ascii characters.
 * ---------------------------------------------------------------
 */
void cli_strupper(char *sp)
{
	int c;

	while (c = *sp)
		*sp++ = TOUPPER(c);
}

/*
 * -------------------------------------------------------
 * Check if string is a Hex number
 *
 * Return:
 *	TRUE	- identifier
 *	FALSE	- otherwise
 * -------------------------------------------------------
 */
int cli_is_hex(char *p)
{
	if (('+' == *p) || ('-' == *p))
		p++;
	if (('0' == *p) && ('X' == TOUPPER(*(p + 1))))
        {
                p = p + 2;
        }
	while (*p && ISXDIGIT_ASCII(*p))
		p++;

	return ((*p) ? FALSE : TRUE);
}

/*
 * -------------------------------------------------------
 * Check if token is a qualifier
 *
 * Return:
 *	TRUE	- qualifier
 *	FALSE	- otherwise
 * -------------------------------------------------------
 */
int cli_is_qualif(char *p)
{
	return (*p == '-');
}


/*
 * -------------------------------------------------------
 * Check if token is an assignment symbol
 *
 * Return:
 *	TRUE	- assignment
 *	FALSE	- otherwise
 * -------------------------------------------------------
 */
int cli_is_assign(char *p)
{
	return (*p == '=');
}

/* ----------------------------------------------
 * Routine to skip white space while reading.
 * Called when a parameter has to be read.
 * The tok_string_extract () doesnt remove
 * starting spaces while reading a string.
 * To make use of that while reading a parameter
 * this has to be called first.
 * ----------------------------------------------
 */

void	skip_white_space(void)
{
	uchar_ptr_t	in_sp;
#	ifdef UTF8_SUPPORTED
	wint_t	ch;
	uchar_ptr_t	next_sp, bufend;
#	endif

	assert(cli_lex_in_ptr);
	in_sp = (uchar_ptr_t)cli_lex_in_ptr->tp;
#	ifdef UTF8_SUPPORTED
	if (gtm_utf8_mode)
	{
		bufend = (uchar_ptr_t)(cli_lex_in_ptr->in_str + cli_lex_in_ptr->buflen);
		for ( ; ; )
		{
			next_sp = UTF8_MBTOWC(in_sp, bufend, ch);
			if (!U_ISSPACE(ch))
				break;
			in_sp = next_sp;
		}
	}
	else
#endif
		while(ISSPACE_ASCII((int)*in_sp))
			in_sp++;

	cli_lex_in_ptr->tp = (char *)in_sp;
}


/*
 * --------------------------------------------
 * Extract one token from a string.
 * Token is anything between the separator characters
 * or separator character itself, if it is '-' or '='.
 *
 * Return:
 *	token Length
 * --------------------------------------------
 */
static int	tok_extract (void)
{
	int	token_len;
	uchar_ptr_t	in_sp, in_next, out_sp, bufend;
#	ifdef UTF8_SUPPORTED
	wint_t		ch;
#	else
	int		ch;
#	endif

	assert(cli_lex_in_ptr);
	skip_white_space();	/* Skip leading blanks */
	in_sp = (uchar_ptr_t)cli_lex_in_ptr->tp;
	bufend = (uchar_ptr_t)&cli_lex_in_ptr->in_str[0] + cli_lex_in_ptr->buflen;

	out_sp = (uchar_ptr_t)cli_token_buf;
	token_len = 0;
	in_next = CLI_GET_CHAR(in_sp, bufend, ch);
	if ('-' == ch || '=' == ch)
	{
		out_sp = CLI_PUT_CHAR(out_sp, ch);
		in_sp = in_next;		/* advance one character */
		token_len = 1;
	} else if (ch)				/* only if something there */
	{
		/* smw if quotable, need to utf8 isspace (BYPASSOK) */
		/* '-' is not a token separator */
		while(ch && !CLI_ISSPACE(ch)
		  && ch != '=')
		{
			out_sp = CLI_PUT_CHAR(out_sp, ch);
			in_sp = in_next;
			in_next = CLI_GET_CHAR(in_next, bufend, ch);
			token_len++;
		}
	}
	ch = 0;
	out_sp = CLI_PUT_CHAR(out_sp, ch);
	cli_lex_in_ptr->tp = (char *)in_sp;
	return(token_len);
}

void cli_lex_in_expand(int in_len)
{
	IN_PARMS	*new_cli_lex_in_ptr;

	new_cli_lex_in_ptr = (IN_PARMS *)malloc(SIZEOF(IN_PARMS) + in_len + 1);	/* + 1 needed for NULL byte */
	new_cli_lex_in_ptr->argc = cli_lex_in_ptr->argc;
	new_cli_lex_in_ptr->argv = cli_lex_in_ptr->argv;
	new_cli_lex_in_ptr->buflen = in_len;		/* + 1 above accounts for NULL byte */
	free(cli_lex_in_ptr);
	cli_lex_in_ptr = new_cli_lex_in_ptr;
}

char *cli_fgets(char *destbuffer, int buffersize, FILE *fp, boolean_t in_tp)
{
	size_t		in_len;
	char		cli_fgets_buffer[MAX_LINE], *retptr = NULL;
#	ifdef UTF8_SUPPORTED
	int		mbc_len, u16_off;
	int32_t		mbc_dest_len;
	UErrorCode	errorcode;
	UChar		*uc_fgets_ret;
	UChar32		uc32_cp;
	UChar		cli_fgets_Ubuffer[MAX_LINE];
	static UFILE	*u_fp;	/* Only used in this routine so not using STATICDEF */
	static FILE	*save_fp; /* Only used in this routine so not using STATICDEF */
#	endif

	assert(MAX_LINE >= buffersize);
	if (in_tp)
		cli_lex_in_ptr->tp = NULL;
#	ifdef UTF8_SUPPORTED
	if (gtm_utf8_mode)
	{
		cli_fgets_Ubuffer[0] = 0;
		if (NULL == save_fp)
			save_fp = fp;
		/* there should be no change in fp as it is currently stdin */
		assert(save_fp == fp);
		/* retain the fact that u_finit has been called once without an intervening
		 * u_fclose so that multiple lines can be read over a pipe on hpux and solaris
		 */
		if (NULL == u_fp)
			u_fp = u_finit(fp, NULL, UTF8_NAME);
		if (NULL != u_fp)
		{
			do
			{	/* no f_ferror */
				uc_fgets_ret = u_fgets(cli_fgets_Ubuffer,
					(int32_t)(SIZEOF(cli_fgets_Ubuffer) / SIZEOF(UChar)) - 1, u_fp);
			} while (NULL == uc_fgets_ret && !u_feof(u_fp) && ferror(fp) && EINTR == errno);
			if (NULL == uc_fgets_ret)
			{
				u_fclose(u_fp);
				/* clear u_fp in case we enter again */
				u_fp = NULL;
				save_fp = NULL;
				return NULL;
			}
			in_len = u_strlen(cli_fgets_Ubuffer);
			in_len = trim_U16_line_term(cli_fgets_Ubuffer, (int)in_len);
			for (u16_off = 0, mbc_len = 0; u16_off < in_len; )
			{
				U16_NEXT(cli_fgets_Ubuffer, u16_off, in_len, uc32_cp);
				mbc_len += U8_LENGTH(uc32_cp);
			}
			if (mbc_len > cli_lex_in_ptr->buflen)
			{
				cli_lex_in_expand(mbc_len);		/* for terminating null */
				buffersize = cli_lex_in_ptr->buflen + 1;
				destbuffer = cli_lex_in_ptr->in_str;
			}
			errorcode = U_ZERO_ERROR;
			u_strToUTF8(destbuffer, buffersize, &mbc_dest_len, cli_fgets_Ubuffer, (int4)in_len + 1, &errorcode);
			if (U_FAILURE(errorcode))
			{
				if (U_BUFFER_OVERFLOW_ERROR == errorcode)
				{	/* truncate so null terminated */
					destbuffer[buffersize - 1] = 0;
					retptr = destbuffer;
				} else
					retptr = NULL;
			} else
				retptr = destbuffer;	/* Repoint to new home */
			if (in_tp)
				cli_lex_in_ptr->tp = retptr;
		}
	} else
	{
#	endif
		cli_fgets_buffer[0] = '\0';
		FGETS_FILE(cli_fgets_buffer, SIZEOF(cli_fgets_buffer), fp, retptr);
		if (NULL != retptr)
		{
			in_len = strlen(cli_fgets_buffer);
			assert(in_len <= MAX_LINE);
			if (cli_lex_in_ptr->buflen < in_len)
			{
				cli_lex_in_expand((int)in_len);
				destbuffer = cli_lex_in_ptr->in_str;
				buffersize = cli_lex_in_ptr->buflen + 1;
			}
			in_len = MIN(in_len, buffersize);
			if ('\n' == cli_fgets_buffer[in_len - 1])
				cli_fgets_buffer[in_len - 1] = '\0';	 /* replace NL */
			memcpy(destbuffer, cli_fgets_buffer, in_len);
			retptr = destbuffer;	/* return proper buffer */
			if (in_tp)
				cli_lex_in_ptr->tp = retptr;
		}
#	ifdef UTF8_SUPPORTED
	}
#	endif
	return retptr;
}

/*
 * -------------------------------------------------------
 * Get token
 *
 * Return:
 *	Token Length
 *
 * Side effects:
 *	set eof to <> 0 for EOF condition.
 * -------------------------------------------------------
 */

int	cli_gettoken (int *eof)
{
	char	*ptr, *ptr_top, *eq_pos, *cur_arg;
	int	arg_no, print_len, token_len, avail, max_space, eq_len;

	assert(cli_lex_in_ptr);
	/* Reading from program argument list */
	if ((1 < cli_lex_in_ptr->argc) && (NULL == cli_lex_in_ptr->tp))
	{
		cli_lex_in_ptr->tp = ptr = cli_lex_in_ptr->in_str;
		/* MUMPS and GT.CM need at least their path length in the command line TODO: why */
		avail = cli_lex_in_ptr->buflen - STRLEN(cli_lex_in_ptr->argv[0]);
		for (ptr_top = ptr + avail, arg_no = 1; (arg_no < cli_lex_in_ptr->argc) && (ptr_top > ptr); arg_no++)
		{	/* Convert arguments into array */
			max_space = (ptr_top - ptr);
			cur_arg = cli_lex_in_ptr->argv[arg_no];
			/* If a qualifier and value pair is present in command line and the value part is having space
			 * we need to add quotes explicitly here as it would have been removed by shell and it is needed by cli
			 * to evaluate the value part with spaces as a single unit .i.e. -label=word1 word2 -> -label="word1 word2"
			 */
			if ((NULL != strchr(cur_arg, ' ')) && (NULL != (eq_pos = strchr(cur_arg, '='))))
			{
				eq_len = eq_pos - cur_arg + 1;
				print_len = SNPRINTF(ptr, max_space, "%s%.*s\"%s\"", (1 < arg_no) ? " " : "", eq_len,
							cur_arg, eq_pos + 1);
			} else
				print_len = SNPRINTF(ptr, max_space, "%s%s", (1 < arg_no) ? " " : "", cur_arg);
			if (max_space <= print_len)
				break;
			ptr += print_len;
		}
	}
	if ((NULL == cli_lex_in_ptr->tp) || (1 > strlen(cli_lex_in_ptr->tp)))
	{
		cli_token_buf[0] = '\0';
		/* cli_fgets can malloc/free cli_lex_in_ptr.  Passing in TRUE as last parameter will do the set
		 * to cli_lex_in_ptr->tp within cli_fgets() after any malloc/free, thus avoiding the problem of
		 * writing to freed memory if the set were done here.
		 */
		cli_fgets(cli_lex_in_ptr->in_str, cli_lex_in_ptr->buflen + 1, stdin, TRUE);
		if (NULL != cli_lex_in_ptr->tp)
			*eof = 0;
		else
		{
			*eof = EOF;
			return (0);
		}
	}
	token_len = tok_extract();
	*eof = (!token_len && (1 < cli_lex_in_ptr->argc));
	return token_len;
}


/*
 * --------------------------------------------
 * Copy next token to the token buffer.
 * Do not advance the token pointer.
 *
 * Return:
 *	Token Length
 *
 * Side effects:
 *	set eof to <> 0 for EOF condition.
 * -------------------------------------------------------
 */
int cli_look_next_token(int *eof)
{
	int tok_len;
	char *old_tp;

	assert(cli_lex_in_ptr);
	if (((char *) NULL == cli_lex_in_ptr->tp) || (!strlen(cli_lex_in_ptr->tp)))
		return(0);
	old_tp = cli_lex_in_ptr->tp;
	tok_len = cli_gettoken(eof);
	cli_lex_in_ptr->tp = old_tp;
	return(tok_len);
}

int cli_look_next_string_token(int *eof)
{
	int tok_len;
	char *old_tp;

	assert(cli_lex_in_ptr);
	if (!strlen(cli_lex_in_ptr->tp))
		return(0);
	old_tp = cli_lex_in_ptr->tp;
	tok_len = cli_get_string_token(eof);
	cli_lex_in_ptr->tp = old_tp;
	return(tok_len);
}

int cli_get_string_token(int *eof)
{
	int		arg_no, token_len;
	char		*from, *to, *stop;
	IN_PARMS 	*new_cli_lex_in_ptr;

	assert(cli_lex_in_ptr);
	/* Reading from program argument list */
	if (cli_lex_in_ptr->argc > 1 && cli_lex_in_ptr->tp == 0)
	{
		cli_lex_in_ptr->tp = cli_lex_in_ptr->in_str;
		/* convert arguments into array */
		for (arg_no = 1; arg_no < cli_lex_in_ptr->argc; arg_no++)
		{
			if (cli_lex_in_ptr->buflen < (STRLEN(cli_lex_in_ptr->in_str) + STRLEN(cli_lex_in_ptr->argv[arg_no]) + 2))
				break;					/* if too long, just truncate */
			if (1 < arg_no) /* 4SCA: strcat adds a space character and a null char */
				strcat(cli_lex_in_ptr->in_str, " ");
			if (cli_has_space(cli_lex_in_ptr->argv[arg_no]))
			{
				from = cli_lex_in_ptr->argv[arg_no];
				to = cli_lex_in_ptr->in_str + STRLEN(cli_lex_in_ptr->in_str) - 1;
				stop = cli_lex_in_ptr->in_str + cli_lex_in_ptr->buflen - 2;
				*to++ = '\"';
				while (('\0' != *from) && (to <= stop))
				{
					if ('\"' == *from)
						*to++ = *from;
					*to++ = *from++;
				}
				*to++ = '\"';
				*to = '\0';
			} else
				STRNCAT(cli_lex_in_ptr->in_str, cli_lex_in_ptr->argv[arg_no],
						cli_lex_in_ptr->buflen - STRLEN(cli_lex_in_ptr->in_str));
		}
	}
	if ((NULL == cli_lex_in_ptr->tp) || (1 > strlen(cli_lex_in_ptr->tp)))
	{
		cli_token_buf[0] = '\0';
		/* cli_fgets can malloc/free cli_lex_in_ptr.  Passing in TRUE as last parameter will do the set
		 * to cli_lex_in_ptr->tp within cli_fgets() after any malloc/free, thus avoiding the problem of
		 * writing to freed memory if the set were done here.
		 */
		cli_fgets(cli_lex_in_ptr->in_str, cli_lex_in_ptr->buflen + 1, stdin, TRUE);
		if (NULL != cli_lex_in_ptr->tp)
			*eof = 0;
		else
		{
			*eof = EOF;
			return (0);
		}
	}
	token_len = tok_string_extract();
	*eof = (!token_len && (1 < cli_lex_in_ptr->argc));
	return token_len;
}

/*
 * -------------------------------------------------------
 * Check if string has space in it
 *
 * Return:
 *      TRUE    - identifier
 *      FALSE   - otherwise
 * -------------------------------------------------------
 */
int cli_has_space(char *p)
{
#	ifdef UTF8_SUPPORTED
	uchar_ptr_t	local_p, next_p, bufend;
	wint_t	ch;

	if (gtm_utf8_mode)
	{
		local_p = (uchar_ptr_t)p;
		bufend = local_p + strlen(p);
		while (local_p)
		{
			next_p = UTF8_MBTOWC(local_p, bufend, ch);
			if (!ch || U_ISSPACE(ch))
				break;
			local_p = next_p;
		}
		p = (char *)local_p;
	}
	else
#	endif
		while (*p && !ISSPACE_ASCII(*p))
			p++;
	return ((*p) ? (TRUE) : (FALSE));
}
