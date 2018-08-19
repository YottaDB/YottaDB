/****************************************************************
 *								*
 * Copyright (c) 2001-2016 Fidelity National Information	*
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

#include "arit.h"
#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "collseq.h"
#include "copy.h"
#include "stringpool.h"
#include "do_xform.h"
#include "format_targ_key.h"

GBLREF	gv_namehead	*gv_target;

static readonly unsigned char pos_code[100] =
{
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
	0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
	0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
	0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
	0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
	0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
	0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
	0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a
};

static readonly unsigned char neg_code[100] =
{
	0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8, 0xf7, 0xf6, 0xf5,
	0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8, 0xe7, 0xe6, 0xe5,
	0xde, 0xdd, 0xdc, 0xdb, 0xda, 0xd9, 0xd8, 0xd7, 0xd6, 0xd5,
	0xce, 0xcd, 0xcc, 0xcb, 0xca, 0xc9, 0xc8, 0xc7, 0xc6, 0xc5,
	0xbe, 0xbd, 0xbc, 0xbb, 0xba, 0xb9, 0xb8, 0xb7, 0xb6, 0xb5,
	0xae, 0xad, 0xac, 0xab, 0xaa, 0xa9, 0xa8, 0xa7, 0xa6, 0xa5,
	0x9e, 0x9d, 0x9c, 0x9b, 0x9a, 0x99, 0x98, 0x97, 0x96, 0x95,
	0x8e, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88, 0x87, 0x86, 0x85,
	0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78, 0x77, 0x76, 0x75,
	0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66, 0x65
};

unsigned char *mval2subsc(mval *in_val, gv_key *out_key, boolean_t std_null_coll)
{
	boolean_t	is_negative;
	unsigned char	buf1[MAX_KEY_SZ + 1], ch, *cvt_table, *in_ptr, *out_ptr;
	unsigned char	*tm, temp_mantissa[NUM_DEC_DG_2L / 2 + 3];	/* Need 1 byte for each two digits.  Add 3 bytes slop */
	mstr		mstr_ch, mstr_buf1;
	int4		mt, mw, mx;
	uint4		mvt;	/* Local copy of mvtype, bit ands use a int4, so do conversion once */
	unsigned int	digs, exp_val;
	int		tmp_len, avail_bytes;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	/* The below assert is an attempt to catch between some and many of the cases where the mvtype is
	 * not an accurate representation of the content. This can happen (and has) when there is a bug in
	 * op_svput() where only the string portion of an mval is set without (re)setting the type leading
	 * to numeric or num-approx values that do not represent reality and causing trouble especially with
	 * subscripts. In that example, "-1" was left as a string with MV_NUM_APPROX on which caused a lot
	 * of trouble with $ORDER in a database when -1 was treated as a string. This assert is not a 100%
	 * catchall of invalid settings but it provides at least some barrier. A full barrier would require
	 * complete conversion which is a bit expensive to always re-do at this point - even in a dbg version.
	 * There are exceptions though and that is if the caller is op_fnview or op_fnzatransform. In that
	 * case, it could set MV_NUM_APPROX to indicate a number needs to be treated as a string subscript.
	 * Skip that in the assert.
	 */
	assert(!(MV_NUM_APPROX & in_val->mvtype) || (NUM_DEC_DG_2L < in_val->str.len) || !val_iscan(in_val)
		|| TREF(skip_mv_num_approx_assert));
	out_ptr = out_key->base + out_key->end;
	if (TREF(transform) && gv_target->nct)
	{
		MV_FORCE_STR(in_val);
		mvt = in_val->mvtype | MV_NUM_APPROX;
	} else
		mvt = (uint4)in_val->mvtype;
	if (!(mvt & (MV_NM | MV_NUM_APPROX)))
	{	/* Not currently in numeric form.  Is it cannonical? */
		if (val_iscan(in_val))
		{	/* Yes, convert it to numeric */
			(void)s2n(in_val);
			mvt = in_val->mvtype;
			assert(mvt & MV_NM);
		} else
		{	/* No, not numeric.  Note the fact for future reference */
			mvt = in_val->mvtype |= MV_NUM_APPROX;
		}
	}
	if (mvt & MV_NUM_APPROX)
	{	/* It's a string */
		in_ptr = (unsigned char *)in_val->str.addr;
		tmp_len = in_val->str.len;
		if (TREF(transform) && gv_target->collseq)
		{
			mstr_ch.len = tmp_len;
			mstr_ch.addr = (char *)in_ptr;
			mstr_buf1.len = SIZEOF(buf1);
			mstr_buf1.addr = (char *)buf1;
			do_xform(gv_target->collseq, XFORM, &mstr_ch, &mstr_buf1, &tmp_len);
			in_ptr = (unsigned char*)mstr_buf1.addr; /* mstr_buf1.addr is used just in case it is
								    reallocated by the XFORM routine */
		}
		/* Find out how much space is needed at a minimum to store the subscript representation of this string.
		 * That would be STR_SUB_PREFIX + input string + at most TWO KEY_DELIMITER.
		 * Assuming this, compute how much space would still be available in the out_key before reaching the top.
		 * If this is negative, we have to signal a GVSUBOFLOW error.
		 * If this is positive and the input string contains 0x00 or 0x01, we would need additional bytes to
		 * 	store the STR_SUB_ESCAPE byte. Decrement the available space until it becomes zero
		 *	at which point issue a GVSUBOFLOW error as well.
		 */
		avail_bytes = out_key->top - (out_key->end + tmp_len + 3);
		if (0 > avail_bytes)
			ISSUE_GVSUBOFLOW_ERROR(out_key, KEY_COMPLETE_FALSE);
		if (0 < tmp_len)
		{
			*out_ptr++ = STR_SUB_PREFIX;
			do
			{
				ch = *in_ptr++;
				if (ch <= 1)
				{
					*out_ptr++ = STR_SUB_ESCAPE;
					if (0 > --avail_bytes)
					{
						/* Ensure input key to format_targ_key is double null terminated */
						assert(STR_SUB_PREFIX == out_key->base[out_key->end]);
						out_key->base[out_key->end] = KEY_DELIMITER;
						ISSUE_GVSUBOFLOW_ERROR(out_key, KEY_COMPLETE_FALSE);
					}
					ch++;	/* promote character */
				}
				*out_ptr++ = ch;
			} while (--tmp_len > 0);
		} else
			*out_ptr++ = (0 == std_null_coll) ? STR_SUB_PREFIX : SUBSCRIPT_STDCOL_NULL;
		goto ALLDONE;
	}
	/* Its a number, is it an integer? But before this assert that we have enough allocated space in the key
	 * to store the maximum possible numeric subscript and two terminating 0s at the end of the key */
	assert((MAX_GVKEY_PADDING_LEN + 1) <= (int)(out_key->top - out_key->end));
	if (mvt & MV_INT)
	{	/* Yes, its an integer, convert it */
		is_negative = FALSE;
		cvt_table = pos_code;
		if (0 > (mt = in_val->m[1]))
		{
			is_negative = TRUE;
			cvt_table = neg_code;
			mt = -mt;
		} else  if (0 == mt)
		{
			*out_ptr++ = SUBSCRIPT_ZERO;
			goto ALLDONE;
		}
		if (10 > mt)
		{
			*out_ptr++ = (unsigned char)(is_negative ? ~(SUBSCRIPT_BIAS - 2) : (SUBSCRIPT_BIAS - 2));
			*out_ptr++ = cvt_table[mt * 10];
			goto FINISH_NUMBER;
		}
		if (100 > mt)
		{
			*out_ptr++ = (unsigned char)(is_negative ? ~(SUBSCRIPT_BIAS - 1) : (SUBSCRIPT_BIAS - 1));
			*out_ptr++ = cvt_table[mt];
			goto FINISH_NUMBER;
		}
		tm = temp_mantissa;
		if (1000 > mt)
		{
			exp_val = SUBSCRIPT_BIAS;
			goto ODD_INTEGER;
		}
		if (10000 > mt)
		{
			exp_val = SUBSCRIPT_BIAS + 1;
			goto EVEN_INTEGER;
		}
		if (100000 > mt)
		{
			exp_val = SUBSCRIPT_BIAS + 2;
			goto ODD_INTEGER;
		}
		if (1000000 > mt)
		{
			exp_val = SUBSCRIPT_BIAS + 3;
			goto EVEN_INTEGER;
		}
		if (10000000 > mt)
		{
			exp_val = SUBSCRIPT_BIAS + 4;
			goto ODD_INTEGER;
		}
		if (100000000 > mt)
		{
			exp_val = SUBSCRIPT_BIAS + 5;
			goto EVEN_INTEGER;
		}
		exp_val = SUBSCRIPT_BIAS + 6;
ODD_INTEGER:
		*out_ptr++ = is_negative ? ~(exp_val) : (exp_val);
		mw = mx = mt / 10;
		mw *= 10;
		mw = mt - mw;
		mt = mx;
		if (mw)
		{
			*tm++ = cvt_table[mw * 10];
			goto FINISH_INTEGERS;
		}
		goto KEEP_STRIPING;
EVEN_INTEGER:
		*out_ptr++ = is_negative ? ~(exp_val) : (exp_val);
KEEP_STRIPING:
		while (mt)
		{
			mw = mx = mt / 100;
			mw *= 100;
			mw = mt - mw;
			mt = mx;
			if (mw)
			{
				*tm++ = cvt_table[mw];
				break;
			}
		}
FINISH_INTEGERS:
		while (mt)
		{
			mw = mx = mt / 100;
			mw *= 100;
			mw = mt - mw;
			*tm++ = cvt_table[mw];
			mt = mx;
		}
		while (tm > temp_mantissa)
			*out_ptr++ = *--tm;
		goto FINISH_NUMBER;
	}
	/* Convert 18 digit number */
	cvt_table = pos_code;
	if (0 != (is_negative = in_val->sgn))
		cvt_table = neg_code;
	*out_ptr++ = is_negative ? ~(in_val->e - MV_XBIAS + SUBSCRIPT_BIAS) : (in_val->e - MV_XBIAS + SUBSCRIPT_BIAS);
	mt = in_val->m[1];
	mw = in_val->m[0];
	/* Strip top two digits */
	mx = mt / (MANT_HI / 100);
	*out_ptr++ = cvt_table[mx];
	mt = (mt - (mx * (MANT_HI / 100))) * 100;
	/*
	 * The two msd's have now been converted.  The maximum number of
	 * data remaining is 7 digits in "mt" and 9 digits in "mw".
	 * If mw is zero, then we should just grind out mt till we are done
	 */
	if (0 == mw)
		goto LAST_LONGWORD;
	/* there are more than 7 digits left.  First, we will put 8 digits in mt, (leaving 8 digits in mw) */
	mx = mw / (MANT_HI / 10);
	mt += mx * 10;
	mw = (mw - (mx * (MANT_HI / 10))) * 10;
	if (0 == mw)
		goto LAST_LONGWORD;
	for (digs = 0;  digs < 4;  digs++)
	{
		mx = mt / (MANT_HI / 100);
		*out_ptr++ = cvt_table[mx];
		mt = (mt - (mx * (MANT_HI / 100))) * 100;
	}
	mt = mw;
LAST_LONGWORD:
	while (mt)
	{
		mx = mt / (MANT_HI / 100);
		*out_ptr++ = cvt_table[mx];
		mt = (mt - (mx * (MANT_HI / 100))) * 100;
	}
FINISH_NUMBER:
	if (is_negative)
		*out_ptr++ = NEG_MNTSSA_END;
ALLDONE:
	*out_ptr++ = KEY_DELIMITER;
	*out_ptr = KEY_DELIMITER;
	out_key->prev = out_key->end;
	out_key->end = out_ptr - out_key->base;
	/* Check if after adding the current subscript and the second terminating NULL byte, there is still
	 * MAX_GVKEY_PADDING_LEN bytes (allocated additionally as part of the DBKEYSIZE macro) left at the end.
	 * If not, we have overflown the original max-key-size length. Issue error.
	 */
	if ((MAX_GVKEY_PADDING_LEN + 1) > (int)(out_key->top - out_key->end))
		ISSUE_GVSUBOFLOW_ERROR(out_key, KEY_COMPLETE_FALSE);
	return out_ptr;
}
