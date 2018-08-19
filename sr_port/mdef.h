/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017-2018 YottaDB LLC. and/or its subsidiaries.*
 * All rights reserved.						*
 *								*
 * Copyright (c) 2017-2018 Stephen L Johnson.			*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/


#ifndef MDEF_included
#define MDEF_included

#ifdef __clang__
#define CLANG_SCA_ANALYZER_NORETURN __attribute__((analyzer_noreturn))
#else
#define CLANG_SCA_ANALYZER_NORETURN
#endif

#  define MSTR_CONST(name, string)		mstr name = {0, LEN_AND_LIT(string)}
#  define MSTR_DEF(name, length, string)	mstr name = {0, length, string}
#  define MIDENT_CONST(name, string)	    mident name = {0, LEN_AND_LIT(string)}
#  define MIDENT_DEF(name, length, string)      mident name = {0, length, string}

#define GET_MSTR_LEN(X, Y)	GET_ULONG(X, Y)
#define PUT_MSTR_LEN(X, Y)	PUT_ULONG(X, Y)

#define	MEMVCMP(STR1, STR1LEN, STR2, STR2LEN, RESULT)							\
{													\
	int	lcl_str1Len, lcl_str2Len;								\
	int	lcl_minLen, lcl_retVal, lcl_retVal2;							\
													\
	lcl_str1Len = STR1LEN;										\
	lcl_str2Len = STR2LEN;										\
	if (lcl_str1Len < lcl_str2Len)									\
	{												\
		lcl_minLen = lcl_str1Len;								\
		lcl_retVal = -1;									\
	} else if (lcl_str1Len > lcl_str2Len)								\
	{												\
		lcl_minLen = lcl_str2Len;								\
		lcl_retVal = 1;										\
	} else												\
	{												\
		lcl_minLen = lcl_str1Len;								\
		lcl_retVal = 0;										\
	}												\
	RESULT = (0 == (lcl_retVal2 = memcmp(STR1, STR2, lcl_minLen))) ? lcl_retVal : lcl_retVal2;	\
}

/* There are 2 MSTR*CMP macros. One is if the parameters are available as MSTRs and another if the parameters
 * are available as MSTR pointers. Use whichever is appropriate as it saves cycles.
 */
#define MSTRP_CMP(x, y, result)	MEMVCMP((x)->addr, (x)->len, (y)->addr, (y)->len, result)
#define MSTR_CMP(x, y, result)	MEMVCMP((x).addr, (x).len, (y).addr, (y).len, result)
#define MSTR_EQ(x, y)		(((x)->len == (y)->len) && !memcmp((x)->addr, (y)->addr, (x)->len))

#include <sys/types.h>

typedef int 		int4;		/* 4-byte signed integer */
typedef unsigned int 	uint4;		/* 4-byte unsigned integer */

#define sssize_t	size_t

/* If ever the following macro (SHMDT) is expanded to a multi-line macro, care should be taken to save the errno immediately after
 * the "shmdt" system call invocation to avoid errno from being mutated by subsequent system calls.
 */
#define SHMDT(X)	shmdt((void *)(X))

/* Because shmget and semget returns -1 to indicate error, -1 can never be a valid shmid or semid */
#define INVALID_SEMID			-1
#define INVALID_SHMID 			-1L

/* constant needed for FIFO - OS390 redefines in mdefsp.h */
#define FIFO_PERMISSION		010666 /* fifo with RW permissions for owner, group, other */

#include <inttypes.h>
#include <stdarg.h>

#include "mdefsa.h"
#include "gtm_common_defs.h"
#include "mdefsp.h"
#include "gtm_sizeof.h"
#include "gtm_threadgbl.h"
#include "ydbmerrors.h"
#include "ydberrors.h"

/* Anchor for thread-global structure rather than individual global vars */
GBLREF void	*gtm_threadgbl;		/* Accessed through TREF macro in gtm_threadgbl.h */

#if defined(DEBUG) || defined(STATIC_ANALYSIS_NORETURN)
error_def(ERR_ASSERT);
# define assert(x) ((x) ? 1 : rts_error_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_ASSERT, 5, LEN_AND_LIT(__FILE__), __LINE__,		\
						(SIZEOF(#x) - 1), (#x)))
# ifdef UNIX
#  define GTMDBGFLAGS_ENABLED
# endif
#else
# define assert(x)
#endif

#ifdef GTM64
# define lvaddr "%016lx"
#else
# define lvaddr "%08lx"
#endif

/* Need to set a flag to 0 or 1 depending on whether AUTORELINK_SUPPORTED is set or not. This is needed by gtmpcat so it can
 * tell if autorelink is supported on the platform or not. While this could be done by always setting AUTORELINK_SUPPORTED
 * which a value of 0 or 1, that's not how the code that uses it is structured so create this new variable with a value
 * depending on whether AUTORELINK_SUPPORTED is defined or not. Specifically, this variable is used with a $DEFINE stmt in
 * gtmpcat_field_defs.txt which gtmpcatfldbld.m uses to create the gtmpcat initialization file. This allows us to capture the
 * values of #define constants and put them into gtmpcat initialization files for use by gtmpcat.
 */
#ifdef AUTORELINK_SUPPORTED
# define ARLINK_ENABLED 1
#else
# define ARLINK_ENABLED 0
#endif

/* Define GT.M interlude functions for open, close, pipe, creat and dup system calls. This lets GT.M trace through all file
 * descriptor activity (needed for D9I11-002714). Do this on all UNIX platforms. Note that only the macro GTM_FD_TRACE is
 * defined here. gtm_unistd.h and gtm_fcntl.h define the actual GT.M interlude functions based on this macro.
 */
#if defined(UNIX) && ! defined(STATIC_ANALYSIS)
#	define	GTM_FD_TRACE
#	define	GTM_FD_TRACE_ONLY(X)	X
#else
#	define	GTM_FD_TRACE_ONLY(X)
#endif

/* Define what is an invalid file descriptor in UNIX and VMS. */
#if defined(UNIX)
#	define	FD_INVALID		-1	/* fd of -1 is invalid in UNIX posix calls */
#	define	FD_INVALID_NONPOSIX	FD_INVALID
#else
#	define	FD_INVALID		-1	/* fd of -1 is invalid in VMS if using POSIX interface (open/close etc.) */
#	define	FD_INVALID_NONPOSIX	 0	/* fd of 0 is invalid in VMS if using RMS sys$open calls (non-posix interface) */
#endif

#if defined(UNIX)
#	define	USE_POLL
#	define	POLL_ONLY(X)	X
#	define	SELECT_ONLY(X)
#else
#	define	USE_SELECT
#	define	POLL_ONLY(X)
#	define	SELECT_ONLY(X)	X
#endif

/* INTPTR_T is an integer that has the same length as a pointer on each platform.  Its basic use is for arithmetic
 * or generic parameters.
 */

typedef intptr_t INTPTR_T;
typedef uintptr_t UINTPTR_T;

/* The intszofptr_t type is defined to be basically the same size as an address on the platforms it runs on. So it
   is the same size as INTPTR_T without the connotation of being a pointer. This is used in places where size_t
   or ssize_t would normally be used except they can't be used because they are the wrong size on Alpha systems.
   Classic usage is in places where need consistant integer and pointer sized elements like constructed parameter
   lists or other arrays.
*/
typedef INTPTR_T intszofptr_t;
typedef UINTPTR_T uintszofptr_t;

#ifdef GTM64
#	define USER_STACK_SIZE  8192
#	define VA_ARG_TYPE long
#	define VA_ARG_TYPE_BOOL int
#	define GTM_IS_64BIT		TRUE
#	define GTM_BITNESS_THIS		"64-bit"
#	define GTM_BITNESS_OTHER	"32-bit"
#else
#       define USER_STACK_SIZE  4096
#	define VA_ARG_TYPE int
#	define VA_ARG_TYPE_BOOL int
#	define GTM_IS_64BIT		FALSE
#	define GTM_BITNESS_THIS		"32-bit"
#	define GTM_BITNESS_OTHER	"64-bit"
#endif /* GTM64 */

#ifdef __CYGWIN__
#	define CYGWIN_ONLY(X) X
#else
#	define CYGWIN_ONLY(X)
#endif

#ifdef __linux__
#	define LINUX_ONLY(X) X
#	define NON_LINUX_ONLY(X)
#else
#	define LINUX_ONLY(X)
#	define NON_LINUX_ONLY(X) X
#endif

#ifdef __MVS__
#	define ZOS_ONLY(X) X
#else
#	define ZOS_ONLY(X)
#endif

#ifdef Linux390
#	define Linux390_ONLY(X) X
#else
#	define Linux390_ONLY(X)
#endif

#if !defined(__alpha) && !defined(__sparc) && !defined(__hpux) && !defined(mips) && !defined(__ia64)
#	define UNALIGNED_ACCESS_SUPPORTED
#endif

#if defined(__i386) || defined(__x86_64__) || defined(_AIX) || defined (__sun) || defined(__armv6l__) || defined(__armv7l__)
#	define GTM_PTHREAD
#	define GTM_PTHREAD_ONLY(X) X
#	define NON_GTM_PTHREAD_ONLY(X)
#else
#	define GTM_PTHREAD_ONLY(X)
#	define NON_GTM_PTHREAD_ONLY(X)	X
#endif

#if defined(__ia64)
#	define IA64_ONLY(X)	X
#	define NON_IA64_ONLY(X)
#  ifdef DEBUG
#	define	IA64_DEBUG_ONLY(X)	X
#  else
#	define	IA64_DEBUG_ONLY(X)
#  endif /* DEBUG */
#else
#	define	IA64_ONLY(X)
#	define	NON_IA64_ONLY(X)	X
#	define	IA64_DEBUG_ONLY(X)
#endif/* __ia64 */

/* macro to check that the OFFSET & SIZE of TYPE1.MEMBER1 is identical to that of TYPE2.MEMBER2 */
#define	IS_OFFSET_AND_SIZE_MATCH(TYPE1, MEMBER1, TYPE2, MEMBER2)		\
	(SIZEOF(((TYPE1 *)NULL)->MEMBER1) == SIZEOF(((TYPE2 *)NULL)->MEMBER2))	\
		&& (OFFSETOF(TYPE1, MEMBER1) == OFFSETOF(TYPE2, MEMBER2))

#define	IS_OFFSET_MATCH(TYPE1, MEMBER1, TYPE2, MEMBER2)	(OFFSETOF(TYPE1, MEMBER1) == OFFSETOF(TYPE2, MEMBER2))

#ifdef __x86_64__
#define X86_64_ONLY(x)			x
#define NON_X86_64_ONLY(x)
#else
#define X86_64_ONLY(x)
#define NON_X86_64_ONLY(x)    		x
#endif /* __x86_64__ */

#if defined(__armv6l__) || defined(__armv7l__)
#define ARM32_ONLY(x)			x
#define NON_ARM32_ONLY(x)
#else
#define ARM32_ONLY(x)
#define NON_ARM32_ONLY(x)    		x
#endif /* __armv6l__ || __armv7l__ */

#if defined(__x86_64__) || defined(__armv6l__) || defined(__armv7l__)
#define	X86_64_OR_ARM32_ONLY(x)		x
#define	NON_X86_64_OR_ARM32_ONLY(x)
#else
#define	X86_64_OR_ARM32_ONLY(x)
#define	NON_X86_64_OR_ARM32_ONLY(x)	x
#endif

#if defined(__i386) || defined(__x86_64__) || defined(__ia64) || defined(__MVS__) || defined(Linux390)
#define NON_RISC_ONLY(x)	x
#define RISC_ONLY(x)
#elif defined(__sparc) || defined(_AIX) || defined(__alpha) || defined(__armv6l__) || defined(__armv7l__)
#define RISC_ONLY(x)		x
#define NON_RISC_ONLY(x)
#endif

#if defined(__armv6l__) || defined(__armv7l__)
#	define ARM_ONLY(x)	x
#	define NON_ARM_ONLY(x)
#else
#	define ARM_ONLY(x)
#	define NON_ARM_ONLY(x)	x
#endif

#ifdef _AIX
#       define  AIX_ONLY(X) X
#else
#       define  AIX_ONLY(X)
#endif

#ifdef __sparc
#	define  SPARC_ONLY(X) X
#else
#define  SPARC_ONLY(X)
#endif
#define BITS_PER_UCHAR  8 /* note, C does not require this to be 8, see <limits.h> for definitions of CHAR_BIT and UCHAR_MAX */

#define MAXPOSINT4		((int4)0x7fffffff)
#define	MAX_DIGITS_IN_INT	10	/* maximum number of decimal digits in a  4-byte integer */
#define	MAX_DIGITS_IN_INT8	20	/* maximum number of decimal digits in an 8-byte integer */
#define	MAX_HEX_DIGITS_IN_INT	 8 	/* maximum number of hexadecimal digits in a  4-byte integer */
#define	MAX_HEX_DIGITS_IN_INT8	16	/* maximum number of hexadecimal digits in an 8-byte integer */

#define MAX_DIGITS_IN_EXP       2       /* maximum number of decimal digits in an exponent */
#define MAX_HOST_NAME_LEN	256
#define MAX_LONG_IN_DOUBLE	0xFFFFFFFFFFFFF /*Max Fraction part in IEEE double format*/
#define MAX_INT_IN_BYTE		255

typedef char		bool;
typedef unsigned char	mreg;
typedef int4		mint;

#define PRE_V5_MAX_MIDENT_LEN	8	/* Maximum length of an mident/mname before GT.M V5.0 */
typedef struct
{ /* The old mident structure used before V50FT01 */
	char	c[PRE_V5_MAX_MIDENT_LEN];
} pre_v5_mident;

#define MAX_MIDENT_LEN		31	/* Maximum length of an mident/mname */
typedef mstr		mident;
typedef struct
{ /* Although we use 31 chars, the extra byte is to keep things aligned AND to keep a null terminator byte for places that care */
	char	c[MAX_MIDENT_LEN + 1];
} mident_fixed;
#define mid_len(name)		strlen(&(name)->c[0])	/* callers of mid_len should include gtm_string.h as well */

#define MIDENT_CMP(x,y,result)	MSTRP_CMP(x, y, result)
#define MIDENT_EQ(x,y)		MSTR_EQ(x, y)

#ifdef INT8_NATIVE
#	define NATIVE_WSIZE	8
#else
#	define NATIVE_WSIZE	4
#endif

/* Maximum length of entry reference of the form "label+offset^routine" */
#define MAX_ENTRYREF_LEN	(2 * MAX_MIDENT_LEN + MAX_DIGITS_IN_INT + STR_LIT_LEN("+^"))

/* M name entry used in various structures - variable table (rtnhdr.h), hash table (hashtab_def.h) and
 * global variable (gv_namehead in gdsfhead.h) */
typedef struct
{
	mident 		var_name;	/* var_name.addr points to the actual variable name */
	uint4		hash_code;	/* hash (scrambled) value of the variable name text */
	boolean_t	marked;		/* Used when in hashtable entry for xkill (at least) */
} mname_entry;

/* The M stack frame on all platforms that follow pv-based linkage model (alpha model)
 * contains a pointer to the base of routine's literal section. All such platforms
 * must define HAS_LITERAL_SECT so that the routines that create a new stack frame
 * initialize literal_ptr field apppropriately.
 *
 */
#if defined(__alpha) || defined(_AIX) || defined(__hpux) || defined(__sparc) || defined(__MVS__) || defined(__APPLE__) || \
	(defined(__linux__) && (defined(__ia64) || defined(__x86_64__) || defined(__s390__) || defined(__armv6l__) || defined(__armv7l__)))
#	define HAS_LITERAL_SECT
#endif

typedef long		ulimit_t;	/* NOT int4; the UNIX ulimit function returns a value of type long */

/* Bit definitions for mval type (mvtype) */
#define MV_NM		 1	/* 0x0001 */
#define MV_INT		 2	/* 0x0002
				 * Note: this bit is set for integers and non-integers with <= 3 digits after the decimal point */
#define MV_NUM_MASK	 3	/* 0x0003 (MV_NM | MV_INT) */
#define MV_STR		 4	/* 0x0004 */
#define MV_NUM_APPROX	 8	/* 0x0008 */	/* bit set implies value is guaranteed to be part number, part string */
#define	MV_CANONICAL    16	/* 0x0010
				 * Note: this bit is set currently only for mvals corresponding to local variable subscripts
				 * in lv_tree.c/lv_tree.h. This bit should not be examined/relied-upon anywhere outside lv_tree.c
				 */
#define MV_SYM		32	/* 0x0020 */
#define MV_SUBLIT	64	/* 0x0040 */
#define MV_RETARG      128	/* 0x0080 */
#define MV_UTF_LEN     256	/* 0x0100 */
#define MV_ALIASCONT   512	/* 0x0200 */

#define	MV_INT_OFF			~(MV_INT)			/* Mask to turn off MV_INT */
#define	MV_STR_OFF			~(MV_STR)			/* Mask to turn off MV_STR */
#define	MV_CANONICAL_OFF		~(MV_CANONICAL)			/* Mask to turn off MV_CANONICAL */
#define	MV_UTF_LEN_OFF			~(MV_UTF_LEN)			/* Mask to turn off MV_UTF_LEN */

#define MV_EXT_NUM_MASK	 (MV_NM | MV_INT | MV_CANONICAL)

/* Special definition used when an xnew'd lv_val is moved from a popped symtab to an earlier
 * one so it can be preserved. This flag marks the lv_val as a pointer to the new symtab so
 * multiple references to it can be resolved.
 */
#define MV_LVCOPIED	0xf000

/* A few more special definitions */
#define	MV_LV_TREE	0xf001		/* An "lvTree" structure has its "ident" field set to this special value */

#define MV_XBIAS	62
#define MV_XZERO	 0
#define MV_BIAS	      1000
#define MV_BIAS_PWR	 3

#define NR_REG		16
#define NUL		 0x00
#define SP		 0x20
#define DEL		 0x7f

#define MAX_STRLEN_32K			32767
/* MAX_STRLEN for local variable is changed from 32767 to 1048576 (1 MB) */
#define MAX_STRLEN	 		(1 * 1024 * 1024)  /*maximum GT.M string size (1 MB)*/
#define MAX_DBSTRLEN			(32 * 1024 - 1) /* Maximum database string size */
/* Initial buffer size allocated for a GT.M string which can geometrically be increased upto the size enough to fit in MAX_STRLEN */
#define MAX_STRBUFF_INIT	 	(32 * 1024)

#define MAX_NUM_SIZE			64
#define MAX_FORM_NUM_SUBLEN		128	/* this is enough to hold the largest numeric subscript */
#define PERIODIC_FLUSH_CHECK_INTERVAL	(30 * 1000)

#ifndef __sparc
# define MAX_ARGS			256 /* in formallist */
#else	/* Sparc super frame has room for 256 args, but functions or concatenate are limited to somewhat fewer */
# define MAX_ARGS			242
#endif

#ifdef UNIX
# define MAX_KEY_SZ	1023		/* maximum database key size */
#else
# define MAX_KEY_SZ	255
#endif
# define OLD_MAX_KEY_SZ	255		/* For V5 and earlier, when only 1 byte was used for compression count */
/* The macro ZWR_EXP_RATIO returns the inflated length when converting the internal subscript
 * representation (byte) length to ZWR representation.
 * In "M" mode,
 * 	Worst case is every other character is non-graphic. e.g. $C(128)_"A"_$C(128).
 * In "UTF-8" mode,
 * 	Worst case is with a non-graphic character and every other character is an illegal
 * 	character. Here are the expansion ratios for different ranges of characters.
 * 	------------------------------------------------------------------------------
 * 		Byte pattern			max. expanded 	   input byte	ratio
 * 						output length	 length
 * 	------------------------------------------------------------------------------
 * 		$C(127)_$ZCH(128)_		18			2	 9
 * 		$C(1536)_$ZCH(128)_		19			3	 7
 * 		$C(65279)_$ZCH(128)_		20			4	 5
 * 		$C(917585)_$ZCH(128)_		21			5	 6
 * 		$C(1114111)_$ZCH(128)_		22			5	 6
 * 	------------------------------------------------------------------------------
 * To cover cases of odd numbers of characters, add some buffer.
 *
 * MAX_ZWR_KEY_SZ, on the other hand, needs to be a compile-time constant since it's used in
 * temporary allocation on the stack
 */
GBLREF	boolean_t		gtm_utf8_mode;
#ifdef UNICODE_SUPPORTED
#	define ZWR_EXP_RATIO(X)	((!gtm_utf8_mode) ? (((X) * 6 + 7)) : ((X) * 9 + 11))
#	define MAX_ZWR_KEY_SZ		(MAX_KEY_SZ * 9 + 11)
#	define MAX_ZWR_EXP_RATIO	9
#else
#	define ZWR_EXP_RATIO(X)	((X) * 6 + 7)
#	define MAX_ZWR_KEY_SZ		(MAX_KEY_SZ * 6 + 7)
#	define MAX_ZWR_EXP_RATIO	6
#endif

#define MAX_SYSERR		1000000

unsigned char *n2s(mval *mv_ptr);
char *s2n(mval *u);
mval *underr (mval *start, ...);
mval *underr_strict(mval *start, ...);

#ifdef DEBUG
#	define	DBG_ASSERT(X)	assert(X),
#else
#	define	DBG_ASSERT(X)
#endif

/* Use the "D" format of these MV_FORCE macros only in those places where there is no possibility of the input being undefined */
#define MV_FORCE_STR(X)		(MV_FORCE_DEFINED(X), MV_FORCE_STRD(X))
#define MV_FORCE_STRD(X)	(DBG_ASSERT(MV_DEFINED(X)) (0 == ((X)->mvtype & MV_STR)) ? n2s(X) : NULL)
#define MV_FORCE_NUM(X)		(MV_FORCE_DEFINED(X), MV_FORCE_NUMD(X))
#define MV_FORCE_NUMD(X)	(DBG_ASSERT(MV_DEFINED(X)) (0 == ((X)->mvtype & MV_NM )) ? s2n(X) : NULL)
#define MV_FORCE_BOOL(X)	(MV_FORCE_NUM(X), (X)->m[1] ? TRUE : FALSE)
#define MV_FORCE_INT(M)		(MV_FORCE_DEFINED(M), MV_FORCE_INTD(M))
#define MV_FORCE_INTD(M)	(DBG_ASSERT(MV_DEFINED(M)) (M)->mvtype & MV_INT ? (M)->m[1]/MV_BIAS : mval2i(M))
#define MV_FORCE_UINT(M)	(MV_FORCE_DEFINED(M), MV_FORCE_UINTD(M))
#define MV_FORCE_UINTD(M)	(DBG_ASSERT(MV_DEFINED(M)) (M)->mvtype & MV_INT ? (M)->m[1]/MV_BIAS : mval2ui(M))
#define MV_FORCE_UMVAL(M,I)	(((I) >= 1000000) ? i2usmval((M),(int)(I)) : \
				(void)( (M)->mvtype = MV_NM | MV_INT , (M)->m[1] = (int)(I)*MV_BIAS ))
#define MV_FORCE_MVAL(M,I)	(((I) >= 1000000 || (I) <= -1000000) ? i2mval((M),(int)(I)) : \
				(void)( (M)->mvtype = MV_NM | MV_INT , (M)->m[1] = (int)(I)*MV_BIAS ))
#ifdef GTM64
#define MV_FORCE_ULMVAL(M,L)	(((L) >= 1000000) ? ui82mval((M),(gtm_uint64_t)(L)) : \
				(void)( (M)->mvtype = MV_NM | MV_INT , (M)->m[1] = (int)(L)*MV_BIAS ))
#define MV_FORCE_LMVAL(M,L)	(((L) >= 1000000 || (L) <= -1000000) ? i82mval((M),(gtm_int64_t)(L)) : \
				(void)( (M)->mvtype = MV_NM | MV_INT , (M)->m[1] = (int)(L)*MV_BIAS ))
#else
#define MV_FORCE_ULMVAL		MV_FORCE_UMVAL
#define MV_FORCE_LMVAL		MV_FORCE_MVAL
#endif
#define MV_FORCE_DEFINED(X)	((!MV_DEFINED(X)) ? (X) = underr(X) : (X))
/* Note MV_FORCE_CANONICAL currently only used in op_add() when vars are known to be defined so no MV_FORCE_DEFINED()
   macro has been added. If uses are added, this needs to be revisited. 01/2008 se
*/
#define MV_FORCE_MSTIMEOUT(TMV, TMS, NOTACID)	/* requires a flock of include files especially for TP */		\
MBSTART {					/* also requires threaddef DCL and SETUP*/				\
	double			tmpdouble;										\
															\
	GBLREF uint4		dollar_tlevel;										\
	GBLREF uint4		dollar_trestart;									\
															\
	MV_FORCE_NUM(TMV);												\
	if (NO_M_TIMEOUT == TMV->m[1])											\
		TMS = NO_M_TIMEOUT;											\
	else														\
	{														\
		assert(MV_BIAS >= 1000);        /* if formats change scale may need attention */			\
		if (TMV->mvtype & MV_INT)										\
		{	/* TMV is an integer. m[1] directly has the # of milliseconds we want.				\
			 * If it is negative though, set timeout to 0.							\
			 */												\
			TMS = TMV->m[1];										\
			if (0 > TMS)											\
				TMS = 0;										\
		} else if (0 == TMV->sgn) 	/* if sign is 0 it means TMV is positive */				\
		{	/* Cap positive timeout at MAXPOSINT4 */							\
			tmpdouble = mval2double(TMV) * (double)1000;							\
			TMS = ((double)MAXPOSINT4 >= tmpdouble) ? (int)tmpdouble : (int)MAXPOSINT4;			\
		} else													\
			TMS = 0;		/* sign is not zero, implies TMV is negative, set timeout to 0 */	\
	}														\
	if ((TREF(tpnotacidtime)).m[1] < TMS)										\
		TPNOTACID_CHECK(NOTACID);										\
} MBEND

#define INVOKE_RESTART	rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_TPRETRY);

#define MV_FORCE_CANONICAL(X)	((((X)->mvtype & MV_NM) == 0 ? s2n(X) : 0 ) \
				 ,((X)->mvtype & MV_NUM_APPROX ? (X)->mvtype &= MV_NUM_MASK : 0 ))
#define MV_IS_NUMERIC(X)	(((X)->mvtype & MV_NM) != 0)
#define MV_IS_INT(X)		(((X)->mvtype & MV_INT) != 0)	/* returns TRUE if input has MV_INT bit set */
#define MV_IS_TRUEINT(X, INTVAL_P)	(isint(X, INTVAL_P))	/* returns TRUE if input is a true integer (no fractions) */
#define MV_IS_STRING(X)		(((X)->mvtype & MV_STR) != 0)
#define MV_DEFINED(X)		(((X)->mvtype & (MV_STR | MV_NM)) != 0)
#define MV_IS_CANONICAL(X)	(((X)->mvtype & MV_NM) ? (((X)->mvtype & MV_NUM_APPROX) == 0) : (boolean_t)val_iscan(X))
#define MV_INIT(X)		((X)->mvtype = 0, (X)->fnpc_indx = UNICODE_ONLY((X)->utfcgr_indx =) 0xff)
#define MV_INIT_STRING(X, LEN, ADDR) ((X)->mvtype = MV_STR, (X)->fnpc_indx = UNICODE_ONLY((X)->utfcgr_indx =) 0xff,	\
				      (X)->str.len = INTCAST(LEN), (X)->str.addr = (char *)ADDR)

/* The MVTYPE_IS_* macros are similar to the MV_IS_* macros except that the input is an mvtype instead of an "mval *".
 * In the caller, use appropriate macro depending on available input. Preferable to use the MVTYPE_IS_* variant to avoid
 * the (X)->mvtype dereference */
#define MVTYPE_IS_NUMERIC(X)	(0 != ((X) & MV_NM))
#define MVTYPE_IS_INT(X)	(0 != ((X) & MV_INT))
#define MVTYPE_IS_NUM_APPROX(X)	(0 != ((X) & MV_NUM_APPROX))
#define MVTYPE_IS_STRING(X)	(0 != ((X) & MV_STR))

/* DEFINE_MVAL_LITERAL is intended to be used to define a string mval where the string is a literal or defined with type
 * "readonly".  In other words, the value of the string does not change.  Since we expect all callers of this macro to use
 * ASCII literals, the MV_UTF_LEN bit is set in the type, and the character length is set to the same value as the byte length.
 */
#define DEFINE_MVAL_LITERAL(TYPE, EXPONENT, SIGN, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH)		\
	DEFINE_MVAL_COMMON(TYPE | MV_UTF_LEN, EXPONENT, SIGN, LENGTH, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH)

/* DEFINE_MVAL_STRING is intended to be used to define a string mval where the value of the string can change */
#define DEFINE_MVAL_STRING(TYPE, EXPONENT, SIGN, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH)		\
	DEFINE_MVAL_COMMON(TYPE, EXPONENT, SIGN, 0, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH)

#ifdef BIGENDIAN
# ifdef UNICODE_SUPPORTED
#  ifdef GTM64
#   define DEFINE_MVAL_COMMON(TYPE, EXPONENT, SIGN, UTF_LEN, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH) \
	{TYPE, SIGN, EXPONENT, 0xff, 0xff, {MANT_LOW, MANT_HIGH}, {UTF_LEN, LENGTH, ADDRESS}}
#  else
#   define DEFINE_MVAL_COMMON(TYPE, EXPONENT, SIGN, UTF_LEN, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH) \
	{TYPE, SIGN, EXPONENT, 0xff, 0xff, 0, {MANT_LOW, MANT_HIGH}, {UTF_LEN, LENGTH, ADDRESS}}
#  endif /* GTM64 */
# else
#  define DEFINE_MVAL_COMMON(TYPE, EXPONENT, SIGN, UTF_LEN, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH) \
	{TYPE, SIGN, EXPONENT, 0xff, {MANT_LOW, MANT_HIGH}, {LENGTH, ADDRESS}}
# endif	/* UNICODE */
#else	/* end BIGENDIAN -- start LITTLEENDIAN */
# ifdef UNICODE_SUPPORTED
#  ifdef GTM64
#    define DEFINE_MVAL_COMMON(TYPE, EXPONENT, SIGN, UTF_LEN, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH) \
	{TYPE, EXPONENT, SIGN, 0xff, 0xff, {MANT_LOW, MANT_HIGH}, {UTF_LEN, LENGTH, ADDRESS}}
#  else
#    define DEFINE_MVAL_COMMON(TYPE, EXPONENT, SIGN, UTF_LEN, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH) \
	{TYPE, EXPONENT, SIGN, 0xff, 0xff, 0, {MANT_LOW, MANT_HIGH}, {UTF_LEN, LENGTH, ADDRESS}}
#  endif /* GTM64 */
# else
#  define DEFINE_MVAL_COMMON(TYPE, EXPONENT, SIGN, UTF_LEN, LENGTH, ADDRESS, MANT_LOW, MANT_HIGH) \
	{TYPE, EXPONENT, SIGN, 0xff, MANT_LOW, MANT_HIGH, LENGTH, ADDRESS}
# endif	/* UNICODE */
#endif	/* BIGENDIAN/LITTLEENDIAN */

#define	ASCII_MAX		(unsigned char)0x7F
#define	IS_ASCII(X)		((uint4)(X) <= ASCII_MAX)	/* X can be greater than 255 hence the typecast to uint4 */

#ifdef UNICODE_SUPPORTED
#	define	MV_FORCE_LEN(X)	    ((!((X)->mvtype & MV_UTF_LEN)) 							\
				     ? (utf8_len(&(X)->str), ((X)->mvtype |= MV_UTF_LEN), (X)->str.char_len)		\
				     : (X)->str.char_len)
#	define	MV_FORCE_LEN_DEC(X) ((!((X)->mvtype & MV_UTF_LEN)) 							\
				     ? (utf8_len_dec(&(X)->str), ((X)->mvtype |= MV_UTF_LEN), (X)->str.char_len)	\
				     : (X)->str.char_len)

/* MV_FORCE_LEN_STRICT() is used to ensure that mval is valid in addition to computing the char_len.
 * Note that the validation is always forced even if MV_UTF_LEN is set since the previously computed
 * char_len might have been evaluated in VIEW "NOBADCHAR" setting. */
#	define	MV_FORCE_LEN_STRICT(X) (((X)->str.char_len = UTF8_LEN_STRICT((X)->str.addr, (X)->str.len)), 	\
					 ((X)->mvtype |= MV_UTF_LEN), (X)->str.char_len)

#	define	MV_IS_SINGLEBYTE(X)	(((X)->mvtype & MV_UTF_LEN) && ((X)->str.len == (X)->str.char_len))
#else
#	define MV_FORCE_LEN(X)		((X)->str.len)
#	define MV_FORCE_LEN_DEC(X)	((X)->str.len)
#	define MV_FORCE_LEN_STRICT(X)	((X)->str.len)
#	define MV_IS_SINGLEBYTE(X)	(TRUE)	/* all characters are single-byte in non-Unicode platforms */
#endif

#define DISK_BLOCK_SIZE		512
#define LOG2_DISK_BLOCK_SIZE	9

#ifdef DEBUG
#  define CHECKPOT(MODULUS)			((MODULUS) & ((MODULUS) - 1)) ? GTMASSERT, 0 :
#  define BREAK_IN_PRO__CONTINUE_IN_DBG		continue
#  define DEBUG_ONLY(statement)			statement
#  define DEBUG_ONLY_COMMA(statement)		statement,
#  define PRO_ONLY(statement)
#else
#  define CHECKPOT(MODULUS)
#  define BREAK_IN_PRO__CONTINUE_IN_DBG		break
#  define DEBUG_ONLY(statement)
#  define DEBUG_ONLY_COMMA(statement)
#  define PRO_ONLY(statement)			statement
#endif

/* These are the analogs of the preceding, but are more efficient when the MODULUS is a Power Of Two.
 * One thing to watch for is that VALUE could be 8-byte and MODULUS could be 4-bytes. In that case, we
 * want to return an 8-byte value. So need to typecast MODULUS to 8-bytes before we do "& ~(MODULUS -1)"
 * or else that will be a 4-byte value and cause a bitwise & with an 8-byte value resulting in a truncated
 * 8-byte return value (loss of high order bits). We choose sm_long_t to reflect that type as it is 8-bytes
 * on the 64-bit platforms and 4-bytes on the 32-bit platforms. Choosing gtm_uint64_t unconditionally will
 * make it 8-bytes on the 32-bit platforms too and result in warnings due to the 8-byte value eventually being
 * truncated to 4-bytes by the caller after the return from the below macro.
 */
#define ROUND_UP2(VALUE, MODULUS)	(CHECKPOT(MODULUS) ((VALUE) + ((MODULUS) - 1)) & ~(((sm_long_t)MODULUS) - 1))
#define ROUND_DOWN2(VALUE, MODULUS)	(CHECKPOT(MODULUS) (VALUE) & ~(((sm_long_t)MODULUS) - 1))

/* Length needed to pad out to a given power of 2 boundary */
#define PADLEN(value, bndry) (int)(ROUND_UP2((sm_long_t)(value), bndry) - (sm_long_t)(value))

#define CALLFROM	LEN_AND_LIT(__FILE__), __LINE__
void gtm_assert(int file_name_len, char file_name[], int line_no)				CLANG_SCA_ANALYZER_NORETURN;
int gtm_assert2(int condlen, char *condtext, int file_name_len, char file_name[], int line_no)	CLANG_SCA_ANALYZER_NORETURN;
#define GTMASSERT	(gtm_assert(CALLFROM))
#define assertpro(x) ((x) ? 1 : gtm_assert2((SIZEOF(#x) - 1), (#x), CALLFROM))
#ifdef UNIX
#ifdef DEBUG
/* The below debug only macros are always used in pairs to indicate a window where the code doesn't expect rts_errors to happen.
 * One reason why the code doesn't expect rts_errors is if the logic is complicated enough that having a condition handler for
 * the window is tricky and will not undo the state of various global variables that were modified. An example of such a window
 * is in gvcst_init. If an rts_error happens in this window, an assert will trip in rts_error at which point, the window as well
 * as the rts_error can be re-examined to see whether the rts_error can be removed or the range of the window can be changed.
 */
#define	DBG_MARK_RTS_ERROR_USABLE	{ assert(TREF(rts_error_unusable)); TREF(rts_error_unusable) = FALSE; }
#define	DBG_MARK_RTS_ERROR_UNUSABLE	{ assert(!TREF(rts_error_unusable)); TREF(rts_error_unusable) = TRUE; }
#else
#define	DBG_MARK_RTS_ERROR_USABLE
#define	DBG_MARK_RTS_ERROR_UNUSABLE
#endif

int	rts_error(int argcnt, ...)			CLANG_SCA_ANALYZER_NORETURN;
int	rts_error_csa(void *csa, int argcnt, ...)	CLANG_SCA_ANALYZER_NORETURN;	/* Use CSA_ARG(CSA) for portability */
#define CSA_ARG(CSA)	(CSA),
void	dec_err(uint4 argcnt, ...);
#elif defined(VMS)
#define rts_error_csa	rts_error
#define CSA_ARG(CSA)	/* no csa arg on VMS */
void dec_err(int4 msgnum, ...);
#else
#error unsupported platform
#endif
void stx_error(int in_error, ...);
void stx_error_va(int in_error, va_list args);
void ins_errtriple(int4 in_error);

int4 timeout2msec(int4 timeout);

/* the RTS_ERROR_TEXT macro will stay till all existing references to it have been renamed to RTS_ERROR_{LITERAL,STRING} */
#define	RTS_ERROR_TEXT(STRING)		LENGTH_AND_STRING(STRING)
/* for those who prefer not remembering the order of the length and the literal/string in the rts_error command line */
#define	RTS_ERROR_LITERAL(LITERAL)	LENGTH_AND_LITERAL(LITERAL)
#define	RTS_ERROR_STRING(STRING)	LENGTH_AND_STRING(STRING)
#define RTS_ERROR_MSTR(MSTR)		(MSTR)->len, (MSTR)->addr
#define RTS_ERROR_MVAL(MVAL)		RTS_ERROR_MSTR(&(MVAL)->str)

#define	SET_PROCESS_EXITING_TRUE						\
{										\
	GBLREF	int		process_exiting;				\
	GBLREF	intrpt_state_t	intrpt_ok_state;				\
										\
	/* Since the process is exiting at this point, we want to clear the 	\
	 * interrupt-deferring mode to prevent issues with nesting interrupt-	\
	 * deferring states and timer delivery.					\
	 */									\
	process_exiting = TRUE;							\
	intrpt_ok_state = INTRPT_OK_TO_INTERRUPT;				\
}

/* *********************************************************************************************************** */
/*		   Frequently used len + str combinations in macro form.				       */
/* *********************************************************************************************************** */
#define	DB_STR_LEN(reg)			(reg)->dyn.addr->fname, (reg)->dyn.addr->fname_len
#define	DB_LEN_STR(reg)			(reg)->dyn.addr->fname_len, (reg)->dyn.addr->fname
#define	REG_STR_LEN(reg)		(reg)->rname, (reg)->rname_len
#define	REG_LEN_STR(reg)		(reg)->rname_len, (reg)->rname
#define	SEG_LEN_STR(seg)		(seg)->sname_len, (seg)->sname
#define	JNL_STR_LEN(csd)		(csd)->jnl_file_name, (csd)->jnl_file_len
#define	JNL_LEN_STR(csd)		(csd)->jnl_file_len, (csd)->jnl_file_name

#define	FAB_LEN_STR(fab)		(fab)->fab$b_fns, (fab)->fab$l_fna
/* *********************************************************************************************************** */

#ifdef DEBUG
/* Original debug code has been removed since it was superfluous and did not work on all platforms. SE 03/01 */
# define SET_TRACEABLE_VAR(var,value) var = value
#else
# define SET_TRACEABLE_VAR(var,value) var = value
#endif

/* If this is unix, we have a faster sleep for short sleeps ( < 1 second) than doing a hiber start.
 * Take this chance to define UNIX_ONLY and VMS_ONLY macros.
 */
void m_usleep(int useconds);
#ifdef UNIX
#	define SHORT_SLEEP(x) {assert(1000 > (x)); m_usleep((x) * 1000);}
#else
#	define SHORT_SLEEP(x) hiber_start(x);
#endif

#ifdef UNIX
#	define UNIX_ONLY(X)			X
#	define UNIX_ONLY_COMMA(X)		X,
#	define UNSUPPORTED_PLATFORM_CHECK
	/* z/OS is not able to have dynamic file extensions while running in MM access mode
	 * If multiple processes are accessing the same mapped file, and one process needs to extend/remap the file,
	 * all the other processes must also unmap the file.
	 * This same comment is in the test framework in set_gtm_machtype.csh.  If this comment is updated, also update the other.
	 */
#	if !defined(__MVS__)
#		define MM_FILE_EXT_OK
#	else
#		undef MM_FILE_EXT_OK
#	endif
#else
#	define UNIX_ONLY(X)
#	define UNIX_ONLY_COMMA(X)
#	define UNSUPPORTED_PLATFORM_CHECK	#error UNSUPPORTED PLATFORM
#endif

#define VMS_ONLY(X)
#define VMS_ONLY_COMMA(X)

/* Note the macros below refer to the UNIX Shared Binary Support. Because the
 * support is *specifically* for the UNIX platform, "NON_USHBIN_ONLY()" will
 * also be true for VMS even though that platform does have shared binary support
 * (but it does not have UNIX Shared Binary support). Use "NON_USHBIN_UNIX_ONLY()"
 * for UNIX platforms that do not support Shared Binaries.
 */
#ifdef USHBIN_SUPPORTED
#	define USHBIN_ONLY(X)		X
#	define NON_USHBIN_ONLY(X)
#	define NON_USHBIN_UNIX_ONLY(X)
#else
#	define USHBIN_ONLY(X)
#	define NON_USHBIN_ONLY(X)	X
#	ifdef UNIX
#		define NON_USHBIN_UNIX_ONLY(X)	X
#	else
#		define NON_USHBIN_UNIX_ONLY(X)
#	endif
#endif
/* Autorelink related macros */
#ifdef AUTORELINK_SUPPORTED
#	define ARLINK_ONLY(X)		X
#	define NON_ARLINK_ONLY(X)
#else
#	define ARLINK_ONLY(X)
#	define NON_ARLINK_ONLY(X)	X
#endif
/* Unicode. Although most (all?) UNIX platforms currently support Unicode, that may
 * not always be the case so a separate contingent is defined.
 */
#ifdef UNICODE_SUPPORTED
#	define UNICODE_ONLY(X) X
#	define NON_UNICODE_ONLY(X)
#else
#	define UNICODE_ONLY(X)
#	define NON_UNICODE_ONLY(X) X
#endif

/* Note: LONG_SLEEP *MUST*NOT* be the sleep() function because use of the sleep() function in
 * GT.M causes problems with GT.M's timers on some platforms. Specifically, the sleep() function
 * causes the SIGARLM handler to be silently deleted on Solaris systems (through Solaris 9 at least).
 * This leads to lost timer pops and has the potential for system hangs.
 */
#define LONG_SLEEP(X)		hiber_start((X) * 1000)
#define LONG_SLEEP_MSEC(X)	hiber_start(X)

#define OS_PAGE_SIZE		gtm_os_page_size
#define OS_PAGE_SIZE_DECLARE	GBLREF int4 gtm_os_page_size;
#ifdef VMS
#	define MAX_IO_BLOCK_SIZE	DISK_BLOCK_SIZE
#else
#	define MAX_IO_BLOCK_SIZE	65536
#endif

#ifndef GTM_INT64T_DEFINED
#define GTM_INT64T_DEFINED
   typedef	uint64_t		gtm_uint64_t;
   typedef	int64_t			gtm_int64_t;
#endif

int ceil_log2_32bit(uint4 num);
int ceil_log2_64bit(gtm_uint64_t num);

typedef INTPTR_T	sm_off_t;

/* HPPA latches (used by load_and_clear) must be 16 byte aligned.
 * By allocating 16 bytes, the routines and macros used to access the latch can do the alignment.
 * Since nothing else should follow to avoid cache threshing, this doesn't really waste space.
 * Note that the additional space for this latch is only allocated on HPPA. All other platforms
 * have a "sensible" compare-and-swap type lock using the first two words in the latch.
 */
typedef struct
{
	union
	{
		gtm_uint64_t	pid_imgcnt;		/* Combined atomic (unique) process id used on VMS */
		struct
		{
			volatile int4	latch_pid;	/* (Usually) Process id of latch holder or LOCK_AVAILABLE. On VMS
							   this word may have other values.  */
			volatile int4	latch_word;	/* Extra word associated with lock (sometimes bci lock or image cnt
							   for VMS) */
		} parts;
	} u;
} global_latch_t;
#define latch_image_count latch_word

#define GLOBAL_LATCH_HELD_BY_US(latch) (process_id == (latch)->u.parts.latch_pid)

typedef struct compswap_time_field_struct
{	/* This structure is used where we want to do a compare-n-swap (CAS) on a time value. The CAS interfaces
	 * need an instance of global_latch_t to operate on. We will utilize the "latch_pid" field to hold the
	 * time and the latch_word is unused except on VMS where it will hold 0. Since this structure must be of
	 * a constant size (size of global_latch_t varies), pad the latch with sufficient space to match the
	 * size of global_latch_t's largest size (on HPUX).
	 */
global_latch_t	time_latch;
int4		hp_latch_space[4];	/* padding only on non-hpux systems */
} compswap_time_field;
/* takes value of time() but needs to be 4 byte so can use compswap on it. Not using time_t, as that is an indeterminate size on
 * various platforms. Value is time (in seconds) in a compare/swap updated field so only one process performs a given task in a
 * given interval
 */
#define cas_time time_latch.u.parts.latch_pid

typedef	union gtm_time8_struct
{
	time_t	ctime;		/* For current GTM code sem_ctime field corresponds to creation time */
	int4	filler[2];	/* Filler to ensure size is 2 words on all platforms */
} gtm_time8;

typedef uint4 gtm_time4_t;

typedef struct
{
	sm_off_t	fl;		/* forward link - relative offset from beginning of this element to next element in queue */
	sm_off_t bl; /* backward link - relative offset from beginning of this element to previous element in queue */
} que_ent;			/* this structure is intended to be identical to the first two items in a cache_que_head */

typedef struct
{
	sm_off_t	fl;		/* forward link - relative offset from beginning of this element to next element in queue */
	sm_off_t bl; /* backward link - relative offset from beginning of this element to previous element in queue */
	global_latch_t	latch;	/* required for platforms without atomic operations to modify both fl and bl concurrently;
				 * unused on platforms with such instructions. */
} que_head, cache_que_head;

#define	IS_PTR_ALIGNED(ptr, ptr_base, elemSize)					\
	(0 == ((((sm_uc_ptr_t)(ptr)) - ((sm_uc_ptr_t)(ptr_base))) % elemSize))
#define	IS_PTR_IN_RANGE(ptr, ptr_lo, ptr_hi)								\
	(((sm_uc_ptr_t)(ptr) >= (sm_uc_ptr_t)(ptr_lo)) && ((sm_uc_ptr_t)(ptr) < (sm_uc_ptr_t)(ptr_hi)))

#define	IS_PTR_2BYTE_ALIGNED(ptr)	(0 == (((uintszofptr_t)ptr) & 0x1))
#define	IS_PTR_4BYTE_ALIGNED(ptr)	(0 == (((uintszofptr_t)ptr) & 0x3))
#define	IS_PTR_8BYTE_ALIGNED(ptr)	(0 == (((uintszofptr_t)ptr) & 0x7))

#ifdef DB64
# ifdef __osf__
#  pragma pointer_size(save)
#  pragma pointer_size(long)
# else
#  error UNSUPPORTED PLATFORM
# endif
#endif

typedef que_ent *	que_ent_ptr_t;
typedef que_head *	que_head_ptr_t;

#ifdef DB64
# ifdef __osf__
#  pragma pointer_size(restore)
# endif
#endif

 /* Define 8-bytes as a structure containing 2-byte array of uint4s.  Overlay this structure upon an 8 byte quantity for easy
  * access to the lower or upper 4 bytes using lsb_index and msb_index respectively.
  */
 typedef struct
 {
	uint4	value[2];
 } non_native_uint8;

#  define	BIG_ENDIAN_MARKER	'B'	/* to denote BIG-ENDIAN machine */
#  define	LITTLE_ENDIAN_MARKER	'L'	/* to denote LITTLE-ENDIAN machine */

#ifdef BIGENDIAN
#  define	msb_index		0
#  define	lsb_index		1
#  define	NODE_ENDIANNESS		BIG_ENDIAN_MARKER
#  define	ENDIANTHIS		"BIG"
#  define	ENDIANOTHER		"LITTLE"
#  define	ENDIANTHISJUSTIFY	"   BIG"	/* right justified */
#  define	GTM_IS_LITTLE_ENDIAN	FALSE
#  define	BIGENDIAN_ONLY(X)	X
#  define	LITTLEENDIAN_ONLY(X)
#else
#  define	msb_index		1
#  define	lsb_index		0
#  define	NODE_ENDIANNESS		LITTLE_ENDIAN_MARKER
#  define	ENDIANTHIS		"LITTLE"
#  define	ENDIANOTHER		"BIG"
#  define	ENDIANTHISJUSTIFY	"LITTLE"	/* right justified */
#  define	GTM_IS_LITTLE_ENDIAN	TRUE
#  define	BIGENDIAN_ONLY(X)
#  define	LITTLEENDIAN_ONLY(X)	X
#endif

#ifdef INT8_SUPPORTED
   typedef	gtm_uint64_t		qw_num;
   typedef	gtm_uint64_t		seq_num;	/* Define 8-byte sequence number */
   typedef	gtm_uint64_t		token_num;	/* Define 8-byte token number */
   typedef	gtm_uint64_t		qw_off_t;	/* quad-word offset */
#  define	DWASSIGNQW(A,B)		(A)=(uint4)(B)
#  define	QWASSIGN(A,B)		(A)=(B)
#  define	QWASSIGNDW(A,B)		QWASSIGN((A),(gtm_uint64_t)(B))
#  define	QWASSIGN2DW(A,B,C)	QWASSIGN((A),(gtm_uint64_t)(B) << 32 | (C))
#  define	QWADD(A,B,C)		(A)=(B)+(C)
#  define	QWSUB(A,B,C)		(A)=(B)-(C)
#  define	QWADDDW(A,B,C)		(A)=(B)+(gtm_uint64_t)(C)
#  define	QWSUBDW(A,B,C)		(A)=(B)-(gtm_uint64_t)(C)
#  define	QWINCRBY(A,B)		(A)+=(B)
#  define	QWDECRBY(A,B)		(A)-=(B)
#  define	QWINCRBYDW(A,B)		(A)+=(gtm_uint64_t)(B)
#  define	QWDECRBYDW(A,B)		(A)-=(gtm_uint64_t)(B)
#  define	QWMULBYDW(A,B,C)	(A)=(B)*(C)
#  define	QWDIVIDEBYDW(A,B,Q,R)	{(R)=(int)((A)%(B)); (Q)=(A)/(B);}
#  define	QWMODDW(A,B)		((A)%(B))
#  define	QWLE(A,B)		((A)<=(B))
#  define	QWLT(A,B)		((A)<(B))
#  define	QWGE(A,B)		((A)>=(B))
#  define	QWGT(A,B)		((A)>(B))
#  define	QWEQ(A,B)		((A)==(B))
#  define	QWNE(A,B)		((A)!=(B))
#  define	INT8_PRINT(x)		x
#  define	INT8_PRINTX(x)		x
#  define	INT8_ONLY(x)		x
#else
 typedef struct non_native_uint8	qw_num;
 typedef struct non_native_uint8	seq_num;
 typedef struct non_native_uint8	token_num;
 typedef struct non_native_uint8	qw_off_t;

#  define	DWASSIGNQW(A,B)		(A)=(B).value[lsb_index]
#  define	QWASSIGN(A,B)		(A)=(B)
#  define	QWASSIGNDW(A,B)		{(A).value[msb_index]=0; (A).value[lsb_index]=B;}
#  define	QWASSIGN2DW(A,B,C)	{(A).value[msb_index]=B; (A).value[lsb_index]=C;}
#  define	QWADD(A,B,C)		{									\
						uint4 temp;							\
						temp = (B).value[lsb_index];					\
						(A).value[lsb_index]=(B).value[lsb_index]+(C).value[lsb_index]; \
						(A).value[msb_index]=(B).value[msb_index]+(C).value[msb_index]; \
						if ((A).value[lsb_index] < temp) (A).value[msb_index]++;	\
					}
#  define	QWSUB(A,B,C)		{									\
						uint4 temp;							\
						temp = (B).value[lsb_index];					\
						(A).value[lsb_index]=(B).value[lsb_index]-(C).value[lsb_index]; \
						(A).value[msb_index]=(B).value[msb_index]-(C).value[msb_index]; \
						if ((A).value[lsb_index] > temp) (A).value[msb_index]--;	\
					}
#  define	QWADDDW(A,B,C)		{									\
						uint4 temp;							\
						temp = (B).value[lsb_index];					\
						(A).value[lsb_index]=(B).value[lsb_index]+C;			\
						(A).value[msb_index]=(B).value[msb_index];			\
						if ((A).value[lsb_index] < temp) (A).value[msb_index]++;	\
					}
#  define	QWSUBDW(A,B,C)		{									\
						uint4 temp;							\
						temp = (B).value[lsb_index];					\
						(A).value[lsb_index]=(B).value[lsb_index]-(C);			\
						(A).value[msb_index]=(B).value[msb_index];			\
						if ((A).value[lsb_index] > temp) (A).value[msb_index]--;	\
					}
#  define	QWINCRBY(A,B)		QWADD(A,A,B)
#  define	QWDECRBY(A,B)		QWSUB(A,A,B)
#  define	QWINCRBYDW(A,B)		QWADDDW(A,A,B)
#  define	QWDECRBYDW(A,B)		QWSUBDW(A,A,B)

				/* B should be less than 64K for the QWDIDIVEBYDW, QWMODDW macros to work correctly */

#  define	QWMULBYDW(A,B,C)	{									\
						uint4	bh, bl, ch, cl, temp, temp1, temp2;			\
						(A).value[msb_index] = (B).value[msb_index] * (C);		\
						bl = (B).value[lsb_index] & 0x0000ffff;				\
						bh = ((B).value[lsb_index] & 0xffff0000) >> 16;			\
						cl = (C) & 0x0000ffff;						\
						ch = ((C) & 0xffff0000) >> 16;					\
						(A).value[msb_index] += bh * ch;				\
						(A).value[lsb_index] = bl * cl;					\
						temp = temp1 = bh * cl;						\
						temp += bl * ch;						\
						if (temp1 > temp)						\
							(A).value[msb_index] += 0x00010000;			\
						temp2 = (A).value[lsb_index];					\
						(A).value[lsb_index] += (temp & 0x0000ffff) << 16;		\
						if ((A).value[lsb_index] < temp2)				\
							(A).value[msb_index] ++;				\
						(A).value[msb_index] += (temp & 0xffff0000) >> 16;		\
					}
#  define	QWDIVIDEBYDW(A,B,Q,R)	{									\
						uint4 msbr, lsbq, twoq, twor;					\
						(R) = (A).value[lsb_index] % (B);				\
						lsbq = (A).value[lsb_index] / (B);				\
						msbr = A.value[msb_index] % B;					\
						(Q).value[msb_index] = (A).value[msb_index] / (B);		\
						twoq = ((uint4)-1) / (B);					\
						twor = (((uint4)-1) % (B) + 1) % (B);				\
						if (0 == twor)							\
							twoq++;							\
						(Q).value[lsb_index] = lsbq;					\
						(Q).value[lsb_index] += twoq * msbr;				\
						if ((Q).value[lsb_index] < lsbq)				\
							(Q).value[msb_index]++;					\
						(R) = (R) + (twor * msbr) % (B);				\
						lsbq = (Q).value[lsb_index];					\
						(Q).value[lsb_index] += (twor * msbr) / (B);			\
						if ((R) > (B))							\
						{								\
							(R) -= (B);						\
							(Q).value[lsb_index]++;					\
						}								\
						if ((Q).value[lsb_index] < lsbq)				\
							(Q).value[msb_index]++;					\
					}
#  define	QWMODDW(A,B)		((((A).value[msb_index] % (B)) * (((uint4)-1) % (B) + 1)		\
								+ (A).value[lsb_index]) % (B))
#  define	QWLE(A,B)		((A).value[msb_index] < (B).value[msb_index] ||				\
						((A).value[msb_index] == (B).value[msb_index]			\
							&& (A).value[lsb_index] <= (B).value[lsb_index]))
#  define	QWLT(A,B)		((A).value[msb_index] < (B).value[msb_index] ||				\
						((A).value[msb_index] == (B).value[msb_index]			\
							&& (A).value[lsb_index] < (B).value[lsb_index]))
#  define	QWGE(A,B)		((A).value[msb_index] > (B).value[msb_index] ||				\
						((A).value[msb_index] == (B).value[msb_index]			\
							&& (A).value[lsb_index] >= (B).value[lsb_index]))
#  define	QWGT(A,B)		((A).value[msb_index] > (B).value[msb_index] ||				\
						((A).value[msb_index] == (B).value[msb_index]			\
							&& (A).value[lsb_index] > (B).value[lsb_index]))
#  define	QWEQ(A,B)		((A).value[msb_index] == (B).value[msb_index]				\
							&& (A).value[lsb_index] == (B).value[lsb_index])
#  define	QWNE(A,B)		((A).value[msb_index] != (B).value[msb_index]				\
							|| (A).value[lsb_index] != (B).value[lsb_index])
#  define	INT8_FMT		"%s"
#  define	INT8_FMTX		"[0x%s]"
#  define	INT8_PRINT(x)		(seq_num_ptr = i2ascl(seq_num_str, x),					\
						seq_num_str[seq_num_ptr - &seq_num_str[0]] = '\0', seq_num_str)
#  define	INT8_PRINTX(x)		(seq_num_ptrx = i2asclx(seq_num_strx, x),				\
						seq_num_strx[seq_num_ptrx - &seq_num_strx[0]] = '\0', seq_num_strx)
#  define	INT8_ONLY(x)
#endif

#define	MAX_SEQNO	((seq_num)-1)	/* actually 0xFFFFFFFFFFFFFFFF (max possible seqno) */


/* The HPUX Itanium compiler is giving warnings whenever a cast is being done and there is a potential alignment change */
/* The RECAST macro will eliminate these warnings by first casting to (void *) before the doing the ultimate cast */

#define RECAST(type)	(type)(void_ptr_t)

/* Define some basic types for shared memory (sm) access depending on whether the platform we are    */
/* using is capable of supporting 32 or 64 bit pointers or not.					     */

#if defined(DB64) || defined(GTM64)
#  if defined(__osf__) && defined(__alpha)
#    pragma pointer_size(save)
#    pragma pointer_size(long)
#  endif
  typedef char *char_ptr_t;		/* Define 64 bit pointer to char */
  typedef unsigned char *uchar_ptr_t;	/* Define 64 bit pointer to unsigned char */
  typedef short *short_ptr_t;		/* Define 64 bit pointer to short */
  typedef unsigned short *ushort_ptr_t; /* Define 64 bit pointer to unsigned short */
  typedef int4 *int_ptr_t;		/* Define 64 bit pointer to int */
  typedef volatile int4 *vint_ptr_t;	/* Define 64 bit pointer to volatile int */
  typedef uint4 *uint_ptr_t;		/* Define 64 bit pointer to uint */
  typedef volatile uint4 *vuint_ptr_t;	/* Define 64 bit pointer to volatile uint */
  typedef void *void_ptr_t;		/* Define 64 bit pointer to void */
  typedef qw_num *qw_num_ptr_t;		/* Define 64 bit pointer to qw_num */
  typedef latch_t *latch_ptr_t;		/* Define 64 bit pointer to latch_t */
  typedef ulatch_t *ulatch_ptr_t;	/* Define 64 bit pointer to ulatch_t */

  /* Shared memory connotation */
  typedef char_ptr_t sm_c_ptr_t;	/* Define 64 bit pointer to char */
  typedef uchar_ptr_t sm_uc_ptr_t;	/* Define 64 bit pointer to unsigned char */
  typedef short_ptr_t sm_short_ptr_t;	/* Define 64 bit pointer to short */
  typedef ushort_ptr_t sm_ushort_ptr_t; /* Define 64 bit pointer to unsigned short */
  typedef int_ptr_t sm_int_ptr_t;	/* Define 64 bit pointer to int */
  typedef vint_ptr_t sm_vint_ptr_t;	/* Define 64 bit pointer to volatile int */
  typedef uint_ptr_t sm_uint_ptr_t;	/* Define 64 bit pointer to uint */
  typedef vuint_ptr_t sm_vuint_ptr_t;	/* Define 64 bit pointer to volatile uint */
  typedef gtm_int64_t sm_long_t;	/* Define 64 bit integer type */
  typedef gtm_uint64_t sm_ulong_t;	/* Define 64 bit unsigned integer type */
  typedef global_latch_t *sm_global_latch_ptr_t; /* Define 64 bit pointer to hp_latch */
#  ifdef __osf__
#    pragma pointer_size(restore)
#  endif
  /* The macro FILL8DCL (explained below) is simple on a 64 bit system since all 64 bits
     will be declared and used. */
#  define FILL8DCL(type,name,fillnum) type name
#else
  typedef char *char_ptr_t;		/* Define 32 bit pointer to char */
  typedef unsigned char *uchar_ptr_t;	/* Define 32 bit pointer to unsigned char */
  typedef short *short_ptr_t;		/* Define 32 bit pointer to short */
  typedef unsigned short *ushort_ptr_t; /* Define 32 bit pointer to unsigned short */
  typedef int4 *int_ptr_t;		/* Define 32 bit pointer to int */
  typedef volatile int4 *vint_ptr_t;	/* Define 32 bit pointer to volatile int */
  typedef uint4 *uint_ptr_t;		/* Define 32 bit pointer to uint */
  typedef volatile uint4 *vuint_ptr_t;	/* Define 32 bit pointer to volatile uint */
  typedef void *void_ptr_t;		/* Define 32 bit pointer to void */
  typedef qw_num *qw_num_ptr_t;		/* Define 32 bit pointer to qw_num */
  typedef latch_t *latch_ptr_t;		/* Define 32 bit pointer to latch_t */
  typedef ulatch_t *ulatch_ptr_t;	/* Define 32 bit pointer to ulatch_t */

  /* Shared memory connotation */
  typedef char_ptr_t sm_c_ptr_t;	/* Define 32 bit pointer to char */
  typedef uchar_ptr_t sm_uc_ptr_t;	/* Define 32 bit pointer to unsigned char */
  typedef short_ptr_t sm_short_ptr_t;	/* Define 32 bit pointer to short */
  typedef ushort_ptr_t sm_ushort_ptr_t; /* Define 32 bit pointer to unsigned short */
  typedef int_ptr_t sm_int_ptr_t;	/* Define 32 bit pointer to int */
  typedef vint_ptr_t sm_vint_ptr_t;	/* Define 32 bit pointer to volatile int */
  typedef uint_ptr_t sm_uint_ptr_t;	/* Define 32 bit pointer to uint */
  typedef vuint_ptr_t sm_vuint_ptr_t;	/* Define 32 bit pointer to volatile uint */
  typedef INTPTR_T sm_long_t;		/* Define 32 bit integer type */
  typedef UINTPTR_T sm_ulong_t;		/* Define 32 bit unsigned integer type */
  typedef global_latch_t *sm_global_latch_ptr_t; /* Define 32 bit pointer to hp_latch */
  /* The macro FILL8DCL is used (on a 32 bit system) to provide a filler area of 32 bits and
     the actual 32 bit declared area. Whether the high order word or the low order word of
     the 64 bit area should be filler depends on the endian mode of the machine. This macro
     will be defined to take care of that for us. */
#  ifdef BIGENDIAN
#    define FILL8DCL(type,name,fillnum) type fill##fillnum,name
#  else
#    define FILL8DCL(type,name,fillnum) type name,fill##fillnum
#  endif
#endif

/* Need to define a type for storing pointer differences */
typedef INTPTR_T  ptroff_t;

/* Need to define a consistently sized off_t type. Some platforms it is 4 bytes, others it is
   4 or 8 bytes depending on flags. The following OFF_T macro is setup to allow the size of the
   variable declared by it to always take up 8 bytes for alignment purposes. If the OFF_T_LONG
   value is set, we will expect the size of 'off_t' to be 8 bytes. An assert will be placed in
   gtm.c to verify this.  */

#ifdef OFF_T_LONG
#  define OFF_T(name,fillnum) off_t	name
#else
#  define OFF_T(name,fillnum) FILL8DCL(off_t,name,fillnum)
#endif

/* Type for offsets in journal files.  VMS uses uint4 to get a full 32 bit
   offset for large journal files (OK since doesn't use lseek/etc. for IO.) */

#ifdef OFF_T_LONG
#  define JNL_OFF_T(name,fillnum) off_t	    name
#else
#  ifdef VMS
#    define JNL_OFF_T(name,fillnum) FILL8DCL(uint4,name,fillnum)
#  else
#    define JNL_OFF_T(name,fillnum) FILL8DCL(off_t,name,fillnum)
#  endif
#endif

/* Need to define a consistently sized counter that is controlled by interlocks. The counter
   will occupy 4 bytes in the file header but on some platforms (currently VAX and AXP VMS),
   these counters need to be shorts whereas other platforms would realize a performance
   improvement if they were 32 bits long. So we create another macro in the spirit of the
   FILL8DCL macro above which will always give us a 32 byte entity but will pad a 2 byte
   addressable entity if necessary. If not specified, the default is for 'short' counters. */
#  ifdef CNTR_WORD_32
#    define FILL4DCL(type,name,fillnum) type name
#    define CNTR4DCL(name,fillnum) int4 name
#  else
#    ifdef BIGENDIAN
#      define FILL4DCL(type,name,fillnum) type fill##fillnum,name
#    else
#      define FILL4DCL(type,name,fillnum) type name,fill##fillnum
#    endif
#    define CNTR4DCL(name,fillnum) FILL4DCL(short,name,fillnum)
#  endif

/* For machines with a cache line dependency for locks and such,
   define a macro that can be used to generate padding such that
   fields are in separate cache lines. Note that this macro should
   *NOT* be used in the fileheader as its length expansion is platform
   specific. It should only be used in internal shared memory
   structures that are NOT otherwise placement sensitive.
   A ; is included in the definition instead of when used since an extra ;
   in a structure is not accepted by some compilers.
*/

#ifdef CACHELINE_SIZE
# define CACHELINE_PAD(fieldSize, fillnum) char fill_cacheline##fillnum[CACHELINE_SIZE - (fieldSize)];
#else
# define CACHELINE_PAD(fieldSize, fillnum)
#endif

/* In certain cases we need to conditionally do a CACHELINE pad. For those platforms that
   have load-locked/store-conditional logic, counters that are incremented under interlock
   need to have spacing so they do not interfere with each other. But platforms that do
   NOT have this capability need the spacing on the actual latch used instead. Hence this
   form of padding is conditional.
*/
#if defined(__alpha) || defined(_AIX)
#  define CACHELINE_PAD_COND(fieldSize, fillnum) CACHELINE_PAD(fieldSize, fillnum)
#else
#  define CACHELINE_PAD_COND(fieldSize, fillnum)
#endif

#define MEMCP(dst,src,start,count,limit){					\
	if (start+count > limit)						\
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_CPBEYALLOC);	\
	else									\
		memcpy(dst+start,src,count);					\
}

#ifndef USING_ICONV
typedef enum
{
	NO_XLAT = 0,
	EBCDIC_TO_ASCII,
	ASCII_TO_EBCDIC
} gtm_iconv_t;
#define iconv_t gtm_iconv_t
#endif

#ifdef _AIX
# define VSIG_ATOMIC_T	sig_atomic_t
#else
# define VSIG_ATOMIC_T	volatile sig_atomic_t
#endif

/* For copying va_list items - Linux/390 needs __va_copy */
#ifndef VAR_COPY
#define VAR_COPY(dst, src)   dst = src
#endif

#define NOLICENSE	/* cheap way to obsolete it */

/* integer conversion functions */
void i2hex(UINTPTR_T val, uchar_ptr_t dest, int len);
void i2hexl(qw_num val, uchar_ptr_t dest, int len);
void i2hex_blkfill(int num, uchar_ptr_t addr, int len);
void i2hexl_blkfill(qw_num num, uchar_ptr_t addr, int len);
int i2hex_nofill(int num, uchar_ptr_t addr, int len);
int i2hexl_nofill(qw_num num, uchar_ptr_t addr, int len);

uchar_ptr_t i2ascl(uchar_ptr_t p, qw_num n);
uchar_ptr_t i2asclx(uchar_ptr_t p, qw_num n);
uchar_ptr_t i2asc(uchar_ptr_t p, unsigned int n);

/* ascii conversion functions */
int4 asc2i(uchar_ptr_t p, int4 len);
qw_num asc2l(uchar_ptr_t p, int4 len);
unsigned int asc_hex2i(uchar_ptr_t p, int len);
gtm_uint64_t asc_hex2l(uchar_ptr_t p, int len);

/* This macro converts an integer to a decimal string (a more efficient alternative to i2asc).
 * It is used by format2zwr() which is called a lot during MUPIP EXTRACT (which can be time-consuming
 * for a big database), hence the need to make it efficient.
 */
#define I2A(des, des_len, num)											\
{														\
	if ((unsigned)(num) < 1000)										\
	{ /* perform light-weight conversion of numbers upto 3 digits */					\
		int 	n1, n2; /* digits at the 10th and 100th decimal positions respectively */		\
		n2 = ((num) / 100) % 10;									\
		if (0 != n2)											\
			(des)[(des_len)++] = n2 + '0';								\
		n1 = ((num) / 10) % 10;										\
		if (0 != n1 || 0 != n2)										\
			(des)[(des_len)++] = n1 + '0';								\
		(des)[(des_len)++] = ((num) % 10) + '0';							\
	} else													\
		des_len += (int)(i2asc((uchar_ptr_t)((des) + des_len), num) - (uchar_ptr_t)((des) + des_len));	\
}

/* The following is similar to I2A except that it updates the input pointer directly (no length parameter needed) */
#define I2A_INLINE(des, num)										\
{													\
	if ((unsigned)(num) < 1000)									\
	{ /* perform light-weight conversion of numbers upto 3 digits */				\
		int 	n1, n2; /* digits at the 10th and 100th decimal positions respectively */	\
		n2 = ((num) / 100) % 10;								\
		if (0 != n2)										\
			*des++ = n2 + '0';								\
		n1 = ((num) / 10) % 10;									\
		if (0 != n1 || 0 != n2)									\
			*des++ = n1 + '0';								\
		*des++ = ((num) % 10) + '0';								\
	} else 												\
		des = (char *)i2asc((uchar_ptr_t)des, num);						\
}

/* This macro converts a decimal string to a number (a more efficient alternative to asc2i).
 * It is used by zwr2format() and str2gvargs which is called a lot during MUPIP LOAD (can be time-consuming for a big database).
 */
#define A2I(cp, end, num)											\
{														\
	unsigned char	*cpbase = (unsigned char*)(cp);								\
	char		ch;											\
														\
	for (num = 0; (cp) < (end) && ('0' <= (ch = *((unsigned char*)cp))) && ('9' >= ch); ++(cp))		\
		num = (num) * 10 + (ch - '0');									\
	if (cpbase == ((unsigned char*)cp))									\
		num = -1;											\
}

void double2s(double *dp, mval *v); /* double conversion */
int skpc(char c, int length, char *string);

/* If the below declaration changes, corresponding changes in gtmxc_types.h needs to be done. */
void *gtm_malloc(size_t size);
/* If the below declaration changes, corresponding changes in gtmxc_types.h needs to be done. */
void gtm_free(void *addr);
int gtm_memcmp (const void *, const void *, size_t);
DEBUG_ONLY(void printMallocInfo(void);)
int is_equ(mval *u, mval *v);
char is_ident(mstr *v);
int val_iscan(mval *v);
void mcfree(void);
int4 getprime(int4 n);
void push_parm(unsigned int totalcnt, int truth_value, ...);
void suspend(int sig);
mval *push_mval(mval *arg1);
void mval_lex(mval *v, mstr *output);

int dlopen_libyottadb(int argc, char **argv, char **envp, char *main_func);
int gtm_main(int argc, char **argv, char **envp);
int mupip_main(int argc, char **argv, char **envp);
int dse_main(int argc, char **argv, char **envp);
int lke_main(int argc, char **argv, char **envp);
int gtcm_play_main(int argc, char **argv, char **envp);
int gtcm_server_main(int argc, char **argv, char **envp);
int gtcm_gnp_server_main(int argc, char **argv, char **envp);
int gtcm_shmclean_main(int argc, char **argv, char **envp);
int dbcertify_main(int argc, char **argv, char **envp);
int ftok_main(int argc, char **argv, char **envp);

#define ZTRAP_CODE	0x00000001
#define ZTRAP_ENTRYREF	0x00000002
#define ZTRAP_POP	0x00000004
#define ZTRAP_ADAPTIVE	(ZTRAP_CODE | ZTRAP_ENTRYREF)

#define GTM_BYTESWAP_16(S)		\
	(  (((S) & 0xff00) >> 8)	\
	 | (((S) & 0x00ff) << 8)	\
	)

#define GTM_BYTESWAP_24(L)		\
	(  (((L) & 0xff0000) >> 16)	\
	 | ((L) & 0x00ff00)		\
	 | (((L) & 0x0000ff) << 16)	\
	)

#define GTM_BYTESWAP_32(L)		\
	(  (((L) & 0xff000000) >> 24)	\
	 | (((L) & 0x00ff0000) >>  8)	\
	 | (((L) & 0x0000ff00) <<  8)	\
	 | (((L) & 0x000000ff) << 24)	\
	)

qw_num	gtm_byteswap_64(qw_num num64);
#ifdef INT8_SUPPORTED
#define GTM_BYTESWAP_64(LL)				\
	(  (((LL) & 0xff00000000000000ull) >> 56)	\
	 | (((LL) & 0x00ff000000000000ull) >> 40)	\
	 | (((LL) & 0x0000ff0000000000ull) >> 24)	\
	 | (((LL) & 0x000000ff00000000ull) >>  8)	\
	 | (((LL) & 0x00000000ff000000ull) <<  8)	\
	 | (((LL) & 0x0000000000ff0000ull) << 24)	\
	 | (((LL) & 0x000000000000ff00ull) << 40)	\
	 | (((LL) & 0x00000000000000ffull) << 56)	\
	)
#else
#define	GTM_BYTESWAP_64(LL) gtm_byteswap_64(LL)
#endif

#define ZDIR_FORM_FULLPATH		0x00000000
#define ZDIR_FORM_DIRECTORY		0x00000001
#define IS_VALID_ZDIR_FORM(zdirform)	(ZDIR_FORM_FULLPATH == (zdirform) || ZDIR_FORM_DIRECTORY == (zdirform))

#define MAXNUMLEN 		128	/* from PV_N2S */
#define CENTISECONDS		100	/* VMS lib$day returns 1/100s, we want seconds, use this factor to convert b/n the two */
#define MINUTE			60	/* seconds in a minute */
#define HOUR			3600	/* one hour in seconds 60 * 60 */
#define ONEDAY			86400	/* seconds in a day */
#define MILLISECS_IN_SEC	((int)1E3)	/* millseconds in a second */
#define MICROSECS_IN_SEC		((int)1E6)	/* microseconds in a second */
#define MICROSECS_IN_MSEC	((int)1E3)	/* microseconds in a millisecond */
#define NANOSECS_IN_SEC		((int)1E9)	/* nanoseconds in a second */
#define NANOSECS_IN_MSEC	((int)1E6)	/* nanoseconds in a millisecond */
#define NANOSECS_IN_USEC	((int)1E3)	/* nanoseconds in a microsecond */
#define E_6			((int)1E6)
#define E_9			((int)1E9)
#define E_18			((long long)1E18)

#define ASSERT_IN_RANGE(low, x, high)	assert((low <= x) && (x <= high))

#if defined(VMS)
#define DAYS		6530  /* adjust VMS returned days by this amount; GTM zero time Dec 31, 1840, VMS zero time 7-NOV-1858 */
#define VARLSTCNT1(CNT)			VARLSTCNT(CNT)
#define PUT_SYS_ERRNO(SYS_ERRNO) 	SYS_ERRNO
#elif defined(UNIX)
#define DAYS		47117 /* adjust UNIX returned days (seconds converted to days); UNIX zero time 1970 */
#define VARLSTCNT1(CNT)			VARLSTCNT(CNT + 1)
#define PUT_SYS_ERRNO(SYS_ERRNO) 	0, SYS_ERRNO
#else
#error Unsupported platform
#endif

#define EXIT_NRM	0
#define EXIT_INF	1
#define EXIT_WRN	2
#define EXIT_ERR	4
#define EXIT_RDONLY	8
#define EXIT_MASK 	7
#define MIN_FN_LEN	1
#define MAX_FN_LEN	255
#define V4_MAX_FN_LEN	255	/* required for dbcertify.h */
#define MAX_TRANS_NAME_LEN	257

typedef uint4 		jnl_tm_t;
typedef uint4 		off_jnl_t;
typedef gtm_uint64_t	gtm_off_t;

#define MAXUINT8	((gtm_uint64_t)-1)
#define MAXUINT4	((uint4)-1)
#define MAXUINT2	((unsigned short)-1)

#define MAXINT4		(MAXUINT4/2)
#define	MAXINT2		(MAXUINT2/2)

/* On platforms that support native 8 byte operations (such as Alpha), an assignment to an 8 byte field is atomic. On other
 * platforms, an 8 byte assignment is a sequence of 4 byte operations. On such platforms, use this macro to determine if the
 * change from the current value to the new value provides a consistent view (entirely the pre read, or entirely the post read,
 * and not in between). Any change that causes the most significant 4 bytes to differ can cause inconsistency. In such cases, it
 * may be necessary to grab crit if modifying a shared field.
 */
#ifdef  INT8_NATIVE
#define QWCHANGE_IS_READER_CONSISTENT(FROM8, TO8)	(TRUE)
#else
/* Note: cannot use this macro when FROM8 or TO8 do not have an lvalue (eg.  literal) */
#define QWCHANGE_IS_READER_CONSISTENT(FROM8, TO8)	(((non_native_uint8 *)&(FROM8))->value[msb_index]	\
							 == ((non_native_uint8 *)&(TO8))->value[msb_index])
#endif

#define	MAX_SUPPL_STRMS		16	/* max # of non-supplementary streams that can connect to a supplementary root primary */

#ifdef UNIX	/* Replication instance file related structures */

/* The below macros and typedef are required in "repl_instance.h", "gtmsource.h", "gtmrecv.h" and "repl_msg.h".
 * They are hence included in this common header file
 */
#define	MAX_INSTNAME_LEN	16	/* Max Length of the replication instance name including terminating null character '\0' */
#define	NUM_GTMSRC_LCL		16	/* max number of source servers that can run on a root primary instance.
					 * also the number of gtmsrc_lcl structures in the replication instance file */
#define	NUM_GTMRCV_LCL		16	/* max number of receiver servers that can run at the same time on a supplementary
					 * root primary instance. On a non-supplementary instance, only 1 receiver server can run */
#define	INVALID_SUPPL_STRM	-1	/* stream #s 0 to 15 are the valid ones */
#define	REPL_INST_HDR_SIZE	(SIZEOF(repl_inst_hdr))
#define	GTMSRC_LCL_SIZE		(SIZEOF(gtmsrc_lcl) * NUM_GTMSRC_LCL)			/* size of the gtmsrc_lcl array */
#define	GTMSOURCE_LOCAL_SIZE	(SIZEOF(gtmsource_local_struct) * NUM_GTMSRC_LCL)	/* size of the gtmsource_local array */
#define	REPL_INST_HISTINFO_START	(REPL_INST_HDR_SIZE + GTMSRC_LCL_SIZE)

/* Although we have dedicated 60-bits for the stream specific seqno, it is still a very high value and should not be reached
 * in practice. Therefore, we arbitrarily pick 48-bits as the maximum value for this seqno and assert that the remaining 12 bits
 * are zero in at least our test environments. This way we catch any uninitialized/garbage value usages of this seqno in the code.
 */
#define	IS_VALID_STRM_SEQNO(SEQNO)	(0 == (SEQNO & 0x0FFF000000000000LLU))

/* Given a strm_seqno, determine the corresponding stream# by getting the most significant 4 bits of the 64-bit seqno */
#define	GET_STRM_INDEX(SEQNO)		(DBG_ASSERT(IS_VALID_STRM_SEQNO(SEQNO))		\
					 (((SEQNO) >> 60) & 0xF))

/* Given a 64-bit strm_seqno, determine the corresponding 60-bit stream specific seqno. */
#define	GET_STRM_SEQ60(SEQNO)		(DBG_ASSERT(IS_VALID_STRM_SEQNO(SEQNO))		\
					 ((SEQNO) & (0x0FFFFFFFFFFFFFFFLLU)))

/* Given a 60-bit strm_seqno and 4-bit stream#, this macro returns a unified 64-bit sequence number */
#define	SET_STRM_INDEX(SEQNO, STRM_NO)	(DBG_ASSERT(0 == GET_STRM_INDEX(SEQNO))		\
					 DBG_ASSERT((STRM_NO) <= 0xF)			\
					 ((SEQNO) | (((seq_num)STRM_NO) << 60)))

#define	MAX_NODENAME_LEN	16	/* used by repl_instance.h. A similar macro JPV_LEN_NODE is defined in jnl.h */

#define	UNKNOWN_INSTNAME	"<UNKNOWN>"	/* used in places where instance name is not known (e.g. if pre-V51000 version) */

/* The following defines the structure holding the instance information in a replication instance file.
 * Any changes to this structure might need changes to the ENDIAN_CONVERT_REPL_INST_UUID macro.
 */
typedef struct repl_inst_uuid_struct
{
	unsigned char	created_nodename[MAX_NODENAME_LEN];	/* Nodename on which instance file was created */
	unsigned char	this_instname[MAX_INSTNAME_LEN];	/* Instance name that this file corresponds to */
	uint4		created_time;				/* Time when this instance file was created */
	uint4		creator_pid;				/* Process id that created the instance file */
} repl_inst_uuid;

/* Macro to endian convert an entire "repl_inst_uuid" structure contents given a pointer to the structure */
#define	ENDIAN_CONVERT_REPL_INST_UUID(PTR)					\
{										\
	/* No need to convert "created_nodename" as it is a character array */	\
	/* No need to convert "this_instname" as it is a character array */	\
	/* Endian convert 4-byte "created_time" */				\
	(PTR)->created_time = GTM_BYTESWAP_32((PTR)->created_time);		\
	/* Endian convert 4-byte "creator_pid" */				\
	(PTR)->creator_pid = GTM_BYTESWAP_32((PTR)->creator_pid);		\
}

/* A NULL UUID is denoted by a 0 value for created_time. In that case, other fields are uninitialized and hence unusable.
 * A non-NULL UUID has a non-zero value for created_time. In that case, other fields are guaranteed to have been initialized.
 */
#define	IS_REPL_INST_UUID_NULL(UUID)		(0 == (UUID).created_time)
#define	IS_REPL_INST_UUID_NON_NULL(UUID)	(!IS_REPL_INST_UUID_NULL(UUID))
#define	NULL_INITIALIZE_REPL_INST_UUID(UUID)	(UUID).created_time = 0

/* Lot of code (e.g. repl_inst_dump) relies on "created_nodename" (which can be a string upto MAX_NODENAME_LEN bytes long)
 * being NOT null-terminated at the (MAX_NODENAME_LEN - 1)th byte ONLY if the node name length is LESS THAN the max length.
 * This lets them avoid a scan of the string (to find the real length) in the null-terminated case and safely pass it to
 * any function (e.g. printf etc.) that expects a null-terminated string. Verify that using the below assert.
 */
#ifdef DEBUG
#define	DBG_CHECK_CREATED_NODENAME(PTR)					\
{									\
	int	index, last_byte_non_null;				\
	char	*lclPtr = (char *)PTR;					\
									\
	last_byte_non_null = lclPtr[MAX_NODENAME_LEN -1];		\
	if (last_byte_non_null)						\
	{								\
		for (index = 0; index < MAX_NODENAME_LEN; index++)	\
		{							\
			if (!lclPtr[index])				\
				assert(FALSE);				\
		}							\
	}								\
}
#else
#define	DBG_CHECK_CREATED_NODENAME(PTR)
#endif

/* The following macros define what value the "histinfo_type" member of the repl_histinfo structure gets filled in with */
#define	HISTINFO_TYPE_NORMAL	1	/* A history record generated whenever a root primary starts up */
#define	HISTINFO_TYPE_UPDRESYNC	2	/* A history record generated when a receiver server starts up with -UPDATERESYNC on
					 * a supplementary root primary instance.
					 */
#define	HISTINFO_TYPE_NORESYNC	3	/* A history record generated when a receiver server starts up with -NORESYNC.
					 * Used by a propagating primary supplementary instance to know that "prev_histinfo_num"
					 * has to be recomputed on the receiver side.
					 */

/* The following defines the structure of a history record in the instance file.
 * Any changes to this structure might need changes to the ENDIAN_CONVERT_REPL_HISTINFO macro.
 */
typedef struct repl_histinfo_struct
{	/* Each history record is uniquely defined by the following 5 fields (consider start_seqno and strm_seqno as one field) */
	unsigned char	root_primary_instname[MAX_INSTNAME_LEN];/* the root primary instance that generated this history record */
	seq_num		start_seqno;				/* the first seqno generated in this history record by the
								 *	root primary. In case of a supplementary instance, this
								 * 	seqno is the unified seqno across all streams.
								 */
	seq_num		strm_seqno;				/* the stream specific jnl seqno. this will help identify which
								 * of the potentially 16 streams (0 for local instance, 1 to 15 for
								 * non-local streams) this history record corresponds to.
								 */
	uint4		root_primary_cycle;			/* a copy of the "root_primary_cycle" field in the instance file
								 * header of the root primary when it generated the seqno
								 * "start_seqno". This is needed to distinguish two invocations
								 * of the same instance
								 */
	uint4		creator_pid;				/* pid on rootprimary instance that wrote this history record */
	uint4		created_time;				/* time on rootprimary when this history record was generated */
	int4		histinfo_num;				/* = 'n' if this is the n'th history record in the instance file */
	int4		prev_histinfo_num;			/* = 'n' if the previous history record corresponding to this
								 * stream is the n'th history record in the instance file.
								 */
	char		strm_index;				/* = 0 by default.
								 * = anywhere from 1 to 15 if this history record corresponds to a
								 *	non-supplementary stream of updates.
								 */
	char		history_type;				/* can take any one of the HISTINFO_TYPE_* macro values */
	char		filler_8[2];				/* Filler for 8-byte alignment */
	repl_inst_uuid	lms_group;				/* Non-null only if this instance file is supplementary AND if
								 * the history record has "strm_index" greater than 0.
								 * Null otherwise.  In the non-null case, this field stores
								 * the lms group uuid for this particular non-supplementary
								 * stream. The "created_time" field will be non-zero in this case
								 * else zero (this field will be used to determine whether the
								 * "lms_group" member is valid or not given a history record).
								 */
	int4		last_histinfo_num[MAX_SUPPL_STRMS];	/* a copy of the last_histinfo_num[] array from the instance file
								 * header BEFORE this history record was added to the instance file.
								 */
} repl_histinfo;

/* Macro to endian convert an entire "repl_inst_uuid" structure contents given a pointer to the structure */
#define	ENDIAN_CONVERT_REPL_HISTINFO(PTR)						\
{											\
	/* No need to convert "root_primary_instname" as it is a character array */	\
	/* Endian convert 8-byte "start_seqno" */					\
	(PTR)->start_seqno = GTM_BYTESWAP_64((PTR)->start_seqno);			\
	/* Endian convert 8-byte "strm_seqno" */					\
	(PTR)->strm_seqno = GTM_BYTESWAP_64((PTR)->strm_seqno);				\
	/* Endian convert 4-byte "root_primary_cycle" */				\
	(PTR)->root_primary_cycle = GTM_BYTESWAP_32((PTR)->root_primary_cycle);		\
	/* Endian convert 4-byte "creator_pid" */					\
	(PTR)->creator_pid = GTM_BYTESWAP_32((PTR)->creator_pid);			\
	/* Endian convert 4-byte "created_time" */					\
	(PTR)->created_time = GTM_BYTESWAP_32((PTR)->created_time);			\
	/* Endian convert 4-byte "histinfo_num" */					\
	(PTR)->histinfo_num = GTM_BYTESWAP_32((PTR)->histinfo_num);			\
	/* Endian convert 4-byte "prev_histinfo_num" */					\
	(PTR)->prev_histinfo_num = GTM_BYTESWAP_32((PTR)->prev_histinfo_num);		\
	/* No need to convert "strm_index" as it is a 1-byte character */		\
	/* No need to convert "history_type" as it is a 1-byte character */		\
	/* Endian convert "lms_group" of type "repl_inst_uuid" */			\
	ENDIAN_CONVERT_REPL_INST_UUID(&((PTR)->lms_group));				\
	/* No need to endian convert "last_histinfo_num" as this is not relevant	\
	 * across a replication connection and is regenerated on the receiver anyways.	\
	 */										\
}

#define	INVALID_HISTINFO_NUM	-1	/* 0 is a valid history record number (first element of array) so set it to -1 */
#define	UNKNOWN_HISTINFO_NUM	-2	/* Special value to indicate there is a history record but is not yet part of the
					 * replication instance file and hence does not have a history number yet (this is
					 * assigned by the function "repl_inst_histinfo_add" only when it adds this history
					 * record to the instance file on the receiving instance). This is possible for
					 * example if the history record is in the receive pool waiting for it to be played
					 * by the update process. Currently used by a propagating primary supplementary instance.
					 */

/* The following two macros convert a history record from that of a non-supplementary instance to a supplementary instance
 * and vice versa. The "start_seqno" and "strm_seqno" fields are the ones which are manipulated in these conversions.
 */
#define	CONVERT_NONSUPPL2SUPPL_HISTINFO(HISTINFO, JNLPOOL_CTL)						\
{													\
	/* Until now "start_seqno" actually corresponded to the non-supplementary stream's seqno.	\
	 * Now that we are writing this history record into a supplementary instance file, switch it	\
	 * to be the supplementary seqno (jnlpool_ctl->jnl_seqno). Until now "strm_seqno" was 0.	\
	 * Now switch that to be what "start_seqno" was before.						\
	 */												\
	assert(0 < (HISTINFO)->strm_index);								\
	assert(MAX_SUPPL_STRMS > (HISTINFO)->strm_index);						\
	assert(0 == (HISTINFO)->strm_seqno);								\
	(HISTINFO)->strm_seqno = (HISTINFO)->start_seqno;						\
	(HISTINFO)->start_seqno = (JNLPOOL_CTL)->jnl_seqno;						\
}

#define	CONVERT_SUPPL2NONSUPPL_HISTINFO(HISTINFO)							\
{													\
	/* This macro is invoked just before sending a non-supplementary stream history record 		\
	 * in a supplementary instance back to a non-supplementary instance. The latter does not	\
	 * understand strm_seqnos hence the need to convert.						\
	 */												\
	assert(0 < (HISTINFO).strm_index);								\
	assert(MAX_SUPPL_STRMS > (HISTINFO).strm_index);						\
	assert((HISTINFO).strm_seqno);									\
	(HISTINFO).start_seqno = (HISTINFO).strm_seqno;							\
	(HISTINFO).strm_seqno = 0;									\
}

/* A structure to hold ALL aspects of ONE side (could be local or remote) of a replication connection. */
typedef struct repl_conn_info_struct
{
	int4		proto_ver;		/* The replication communication protocol version of this side of the pipe.
						 * Needs to be "signed" in order to be able to do signed comparisons of this with
						 * the macros REPL_PROTO_VER_DUALSITE (0) and REPL_PROTO_VER_UNINITIALIZED (-1)
						 */
	uint4		jnl_ver;		/* Format of the journal records */
	boolean_t	is_std_null_coll;	/* TRUE if M-standard null collation; FALSE if GT.M null collation */
	boolean_t	trigger_supported;	/* TRUE if supports triggers; FALSE otherwise */
	boolean_t	cross_endian;		/* TRUE if both sides of the replication connection have different endianness */
	boolean_t	endianness_known;	/* TRUE if endianness of other side is known/determined; FALSE until then */
	boolean_t	null_subs_xform;	/* 0 if the null subscript collation is same between the servers
						 * Non-zero (GTMNULL_TO_STDNULL_COLL or STDNULL_TO_GTMNULL_COLL) if different
						 */
	boolean_t	is_supplementary;	/* Whether one side of the connection is a supplementary instance */
	boolean_t	tls_requested;		/* Whether one side of the connection requests SSL/TLS secure communication */
	/* Since this structure is a member of jnlpool_ctl_struct which needs to be 16-byte aligned, align this structure to a
	 * 16-byte boundary.
	 */
	char		filler_16[12];
} repl_conn_info_t;

#endif	/* Replication instance file related structures */

/* Enumerator codes for supported CHSETs in GT.M */
typedef enum
{
	CHSET_M,
	CHSET_UTF8,
	CHSET_UTF16,
	CHSET_UTF16LE,
	CHSET_UTF16BE,
	CHSET_ASCII,
	CHSET_EBCDIC,
	CHSET_BINARY,
	CHSET_MAX_IDX_ALL	/* maximum number of CHSETs supported */
} gtm_chset_t;

#define CHSET_UTF_MIN	CHSET_UTF8
#define CHSET_UTF_MAX	CHSET_UTF16BE
#define CHSET_MAX_IDX	CHSET_ASCII	/* max true CHSETs */

#define	IS_UTF16_CHSET(chset)	((CHSET_UTF16 == (chset)) || (CHSET_UTF16LE == (chset)) || (CHSET_UTF16BE == (chset)))
#define IS_UTF_CHSET(chset) ((CHSET_UTF_MIN <= (chset)) && (CHSET_UTF_MAX >= (chset)))

#define CHK_BOUNDARY_ALIGNMENT(pointer) (((UINTPTR_T)pointer) & (SIZEOF(UINTPTR_T) - 1))

/* Encryption- and TLS-related macros */
#if defined(__ia64) || defined(__i386) || defined(__x86_64__) || defined(__sparc) || defined(_AIX) || defined(__s390__)		\
	|| defined(__armv6l__) || defined(__armv7l__)
# define GTM_TLS
# define GTMTLS_ONLY(X)			X
# define GTMTLS_ONLY_COMMA(X)		, X
# define NON_GTMTLS_ONLY(X)
#else
# define GTMTLS_ONLY(X)
# define GTMTLS_ONLY_COMMA(X)
# define NON_GTMTLS_ONLY(X)		X
#endif

#define GTMCRYPT_HASH_LEN		64
#define GTMCRYPT_RESERVED_HASH_LEN	80
#define GTMCRYPT_HASH_HEX_LEN		GTMCRYPT_HASH_LEN * 2

#define GTMCRYPT_PLUGIN_DIR_NAME	"plugin"
#define GTMCRYPT_UTIL_LIBNAME		"libgtmcryptutil.so"

#define GET_HASH_IN_HEX(in, out, len)						\
{										\
	int i;									\
										\
	assert(0 == len % 2);							\
	for (i = 0; i < len; i+=2)						\
		SPRINTF((char *)out + i, "%02X", (unsigned char)in[i/2]);	\
}

/* Currently triggers are supported only on UNIX */
#if defined(UNIX)
#	define	GTM_TRIGGER
#	define	GTMTRIG_ONLY(X)			X
#	define	NON_GTMTRIG_ONLY(X)
#	define	GTMTRIG_DBG_ONLY(X)		DEBUG_ONLY(X)
#	define	GTM_TRIGGER_DEPTH_MAX		127	/* Maximum depth triggers can nest */
#else
#	define	GTMTRIG_ONLY(X)
#	define	NON_GTMTRIG_ONLY(X)		X
#	define	GTMTRIG_DBG_ONLY(X)
#endif

/* A type definition to hold a range of numbers */
typedef struct gtm_num_range_struct
{
	uint4   min;    /* included in range */
	uint4   max;    /* included in range */
} gtm_num_range_t;

/* Debug FPRINTF with pre and post requisite flushing of appropriate streams */
#ifndef DBGFPF
# define DBGFPF(x)	{flush_pio(); FPRINTF x; FFLUSH(stderr); FFLUSH(stdout);}
#endif

/* Macro used intermittently in code to debug alias code in general. Uncomment the #define below to
 * enable the debugging.
 */
/* #define DEBUG_ALIAS */
#ifdef DEBUG_ALIAS
# define DBGALS(x) DBGFPF(x)
# define DBGALS_ONLY(x) x
#else
# define DBGALS(x)
# define DBGALS_ONLY(x)
#endif

/* Settings for lv_null_subs */
enum
{
	LVNULLSUBS_FIRST = -1,	/* So _NO is 0 to match existing values */
	LVNULLSUBS_NO,		/* No null LV subscripts in SET type cases */
	LVNULLSUBS_OK,		/* Null LV subscripts are allowed */
	LVNULLSUBS_NEVER,	/* LVNULLSUBS_NO plus LV subscripts prohibited in $DATA, $GET, $ORDER, $QUERY, KILL, etc */
	LVNULLSUBS_LAST
};
#define MAX_GVSUBSCRIPTS 32
#define MAX_LVSUBSCRIPTS 32
#define MAX_INDSUBSCRIPTS 32
#define MAX_FOR_STACK 32

#define PUSH_PARM_OVERHEAD	4	/* This extra space in the array is needed because push_parm() is capable of handling 32
					 * arguments, but callg needs to accomidate space for 4 items, namely argument count
					 * (repeated twice), return value, and the truth value. As of this writing ojchildparms.c is
					 * the only module that relies on the extra space.
					 */
#define	MAX_ACTUALS		32	/* Maximum number of arguments allowed in an actuallist. This value also determines
					 * how many parameters are allowed to be passed between M and C.
					 */
#if defined(DEBUG) && defined(UNIX)
#define OPERATOR_LOG_MSG												\
{															\
	error_def(ERR_TEXT);	/* BYPASSOK */										\
	if (ydb_white_box_test_case_enabled && (WBTEST_OPER_LOG_MSG == ydb_white_box_test_case_number))			\
	{														\
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_TEXT, 2, LEN_AND_LIT("Send message to operator log"));	\
	}														\
}
#else
#define OPERATOR_LOG_MSG
#endif

#ifdef GTM_PTHREAD
/* If we detect a case when the signal came to a thread other than the main GT.M thread, this macro will redirect the signal to the
 * main thread if such is defined. Such scenarios is possible, for instance, if we are running along a JVM, which, upon receiving a
 * signal, dispatches a new thread to invoke signal handlers other than its own. The pthread_kill() enables us to target the signal
 * to a specific thread rather than rethrow it to the whole process.
 */
#define FORWARD_SIG_TO_MAIN_THREAD_IF_NEEDED(SIG)								\
{														\
	GBLREF pthread_t	gtm_main_thread_id;								\
	GBLREF boolean_t	gtm_main_thread_id_set;								\
														\
	if (gtm_main_thread_id_set && !pthread_equal(gtm_main_thread_id, pthread_self()))			\
	{	/* Only redirect the signal if the main thread ID has been defined, and we are not that. */	\
		pthread_kill(gtm_main_thread_id, SIG);								\
		return;												\
	}													\
}
#else
#define FORWARD_SIG_TO_MAIN_THREAD_IF_NEEDED(SIG)
#endif

#ifdef DEBUG
# define MVAL_IN_RANGE(V, START, END)	(((char *)(V) >= (char *)(START))					\
				      && ((char *)(V) < ((char *)(START) + (INTPTR_T)(END) * SIZEOF(mval))))
#endif

/* Whenever an M argument (an actual) is skipped (rather than undefined) in a label call, the resulting mval pointer refers to the
 * skiparg, which is a special mval designated for this purpose. This is ensured by OC_NULLEXP, which is the opcode inserted for all
 * skipped arguments.
 */
#define M_ARG_SKIPPED(V)			((V) == (mval *)&skiparg)

/* Ensures that the argument is defined if it was not skipped. */
#define MV_FORCE_DEFINED_UNLESS_SKIPARG(V)	((!M_ARG_SKIPPED(V)) ? (MV_FORCE_DEFINED(V)) : (V))

#ifdef DEBUG
  /* Define macros that are helpful in verifying that functions in libyottadb.so are only invoked
   * by the executables/utilities we expect and not by anything else. For example, libgnpclient.list
   * used to have a list of modules that this library includes and was linked only by mumps and lke.
   * Now libgnpclient.list is nixed (as part of changes that made all utilities use libyottadb.so
   * and reduce their sizes) but we now have an ASSERT_IS_LIBGNPCLIENT check at function entry in all
   * functions that were part of modules in that listing file. Same with the other asserts defined below.
   */
# include "gtmimagename.h"
# define ASSERT_IS_LIBGNPCLIENT		assert(IS_LIBGNPCLIENT)
# define ASSERT_IS_LIBGNPSERVER		assert(IS_LIBGNPSERVER)
# define ASSERT_IS_LIBCMISOCKETTCP	assert(IS_LIBCMISOCKETTCP)
# define ASSERT_IS_LIBGTCM		assert(IS_LIBGTCM)
#else
# define ASSERT_IS_LIBGNPCLIENT
# define ASSERT_IS_LIBGNPSERVER
# define ASSERT_IS_LIBCMISOCKETTCP
# define ASSERT_IS_LIBGTCM
#endif

#endif /* MDEF_included */
