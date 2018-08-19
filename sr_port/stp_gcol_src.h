/****************************************************************
 *								*
 * Copyright (c) 2001-2017 Fidelity National Information	*
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

#include "mdef.h"

#include <stddef.h>
#include "gtm_string.h"
#include "gtm_stdio.h"

#include "gtmio.h"
#include "gdsroot.h"
#include "gdskill.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "fnpc.h"
#ifdef UNICODE_SUPPORTED
# include "utfcgr.h"
#endif
#include "gdscc.h"
#include "cache.h"
#include "comline.h"
#include "compiler.h"
#include "io.h"
#include "jnl.h"
#include "lv_val.h"
#include "subscript.h"
#include "mdq.h"
#include <rtnhdr.h>
#include "mv_stent.h"
#include "stack_frame.h"
#include "stp_parms.h"
#include "stringpool.h"
#include "buddy_list.h"		/* needed for tp.h */
#include "hashtab_int4.h"	/* needed for tp.h */
#include "tp.h"
#include "tp_frame.h"
#include "mlkdef.h"
#include "zbreak.h"
#include "zshow.h"
#include "zwrite.h"
#include "error.h"
#include "stpg_sort.h"
#include "hashtab_objcode.h"
#include "hashtab_str.h"
#include "min_max.h"
#include "alias.h"
#include "gtmimagename.h"
#include "srcline.h"
#include "opcode.h"
#include "glvn_pool.h"
#include "iormdef.h"
#include "localvarmonitor.h"

#ifndef STP_MOVE
GBLDEF int	indr_stp_low_reclaim_passes = 0;
GBLDEF int	rts_stp_low_reclaim_passes = 0;
GBLDEF int	indr_stp_incr_factor = 1;
GBLDEF int	rts_stp_incr_factor = 1;
#else
GBLREF int	indr_stp_low_reclaim_passes;
GBLREF int	rts_stp_low_reclaim_passes;
GBLREF int	indr_stp_incr_factor;
GBLREF int	rts_stp_incr_factor;
#endif

GBLREF mvar 			*mvartab;
GBLREF mlabel 			*mlabtab;
GBLREF int 			mvmax;
GBLREF int			mlmax;
GBLREF int 			mvar_index;
GBLREF hash_table_objcode 	cache_table;
GBLREF unsigned char		*msp, *stackbase, *stacktop, *stackwarn;
GBLREF gtm_uint64_t		stp_array_size;
GBLREF io_log_name		*io_root_log_name;
GBLREF lvzwrite_datablk		*lvzwrite_block;
GBLREF mliteral			literal_chain;
GBLREF mstr			*comline_base, **stp_array;
GBLREF mval			dollar_system, dollar_zerror, dollar_zgbldir, dollar_zstatus;
GBLREF mval			dollar_zyerror, zstep_action, dollar_zinterrupt, dollar_zsource, dollar_ztexit;
GBLREF mv_stent			*mv_chain;
GBLREF sgm_info			*first_sgm_info;
GBLREF spdesc			indr_stringpool, rts_stringpool, stringpool;
GBLREF stack_frame		*frame_pointer;
GBLREF symval			*curr_symval;
GBLREF tp_frame			*tp_pointer;
GBLREF boolean_t		stop_non_mandatory_expansion, expansion_failed, retry_if_expansion_fails;
GBLREF boolean_t		mstr_native_align;
GBLREF zwr_hash_table		*zwrhtab;				/* How we track aliases during zwrites */
GBLREF int4			SPGC_since_LVGC;			/* stringpool GCs since the last dead-data GC */
GBLREF int4			LVGC_interval;				/* dead data GC done every LVGC_interval stringpool GCs */
GBLREF boolean_t		suspend_lvgcol;
GBLREF hash_table_str		*complits_hashtab;
GBLREF mval			*alias_retarg;
GBLREF io_pair			io_std_device;
GTMTRIG_ONLY(GBLREF mval 	dollar_ztwormhole;)
DEBUG_ONLY(GBLREF   boolean_t	ok_to_UNWIND_in_exit_handling;)


OS_PAGE_SIZE_DECLARE

static mstr			**topstr, **array, **arraytop;

error_def(ERR_STPEXPFAIL);
error_def(ERR_STPCRIT);
error_def(ERR_STPOFLOW);

/* See comment inside LV_NODE_KEY_STPG_ADD macro for why the ASSERT_LV_NODE_MSTR_EQUIVALENCE macro does what it does */
#define ASSERT_LV_NODE_MSTR_EQUIVALENCE										\
MBSTART {													\
	assert(OFFSETOF(lvTreeNode, key_len) - OFFSETOF(lvTreeNode, key_mvtype) == OFFSETOF(mstr, len));	\
	assert(SIZEOF(((lvTreeNode *)NULL)->key_len) == SIZEOF(((mstr *)NULL)->len));				\
	assert(OFFSETOF(lvTreeNode, key_addr) - OFFSETOF(lvTreeNode, key_mvtype) == OFFSETOF(mstr, addr));	\
	assert(SIZEOF(((lvTreeNode *)NULL)->key_addr) == SIZEOF(((mstr *)NULL)->addr));				\
} MBEND

#define	LV_NODE_KEY_STPG_ADD(NODE)											\
MBSTART {														\
	/* NODE is of type "lvTreeNode *" or "lvTreeNodeNum *".								\
	 * Only if it is a "lvTreeNode *" (MV_STR bit is set in this case only)						\
	 * should we go ahead with the STPG_ADD.									\
	 */														\
	if (LV_NODE_KEY_IS_STRING(NODE))										\
	{														\
		/* NODE is of type "lvTreeNode *". We need to now pass an mstr pointer to the MSTR_STPG_ADD macro.	\
		 * We cannot create a local mstr since the address of this mstr needs to be different for each		\
		 * NODE passed in to LV_NODE_KEY_STPG_ADD during stp_gcol. Therefore we use an offset inside NODE	\
		 * as the mstr address. Even though NODE is not exactly laid out as an mstr, we ensure the fields	\
		 * that stp_gcol most cares about "mstr->len" and "mstr->addr" are at the exact same offset in		\
		 * both NODE and an "mstr". The equivalent of "mstr->char_len" is not present in NODE but thankfully	\
		 * char_len is not used/touched in stp_gcol and therefore it is ok if something else is present		\
		 * at that offset in the mstr as long as that is exactly 4 bytes long (that happens to be the case	\
		 * in the NODE structure). We assert this below before using an mstr inside NODE.			\
		 * Note: VMS mstr does not have char_len so start accordingly below.					\
		 */													\
		ASSERT_LV_NODE_MSTR_EQUIVALENCE;									\
		/* Note: the typecast below is two-fold (first a "void *" and then a "mstr *"). Not having the "void *"	\
		 * gives compiler warnings in HPUX Itanium since source is 2-byte aligned whereas destination		\
		 * is 8-byte aligned. The void * in between makes the compiler forget the 2-byte alignment.		\
		 */													\
		UNIX_ONLY(MSTR_STPG_ADD((mstr *)(void *)&NODE->key_mvtype));						\
		VMS_ONLY(MSTR_STPG_ADD((mstr *)(void *)&NODE->key_len));						\
	}														\
} MBEND

#define	MVAL_STPG_ADD(MVAL1)							\
{										\
	mval		*lcl_mval;						\
										\
	lcl_mval = MVAL1;							\
	if (MV_IS_STRING(lcl_mval))						\
		MSTR_STPG_ADD(&lcl_mval->str);					\
}

#define MSTR_STPG_PUT(MSTR1)							\
{										\
	int		stp_put_int;						\
										\
	TREE_DEBUG_ONLY(							\
	/* assert that we never add a duplicate mstr as otherwise we will	\
	 * face problems later in PROCESS_CONTIGUOUS_BLOCK macro.		\
	 * this code is currently commented out as it slows down stp_gcol	\
	 * tremendously (O(n^2) algorithm where n is # of mstrs added)		\
	 */									\
		mstr	**curstr;						\
										\
		for (curstr = array; curstr < topstr; curstr++)			\
			assert(*curstr != (MSTR1));				\
	)									\
	assert(topstr < arraytop);						\
	assert(0 < (MSTR1)->len);						\
	/* It would be nice to test for maxlen as well here but that causes	\
	 * some usages of stringpool to fail as other types of stuff are	\
	 * built into the stringppool besides strings.				\
	 */									\
	*topstr++ = MSTR1;							\
	if (topstr >= arraytop)							\
	{									\
		stp_put_int = (int)(topstr - array);				\
		stp_expand_array();						\
		array = stp_array;						\
		topstr = array + stp_put_int;					\
		arraytop = array + stp_array_size;				\
		assert(topstr < arraytop);					\
	}									\
}										\

#ifdef STP_MOVE

#define	MSTR_STPG_ADD(MSTR1)										\
{													\
	mstr		*lcl_mstr;									\
	char		*lcl_addr;									\
													\
	GBLREF	spdesc	stringpool;									\
													\
	lcl_mstr = MSTR1;										\
	if (lcl_mstr->len)										\
	{												\
		lcl_addr = lcl_mstr->addr;								\
		if (IS_PTR_IN_RANGE(lcl_addr, stringpool.base, stringpool.free))	/* BYPASSOK */	\
		{											\
			MSTR_STPG_PUT(lcl_mstr);							\
		} else if (IS_PTR_IN_RANGE(lcl_addr, stp_move_from, stp_move_to))			\
		{											\
			MSTR_STPG_PUT(lcl_mstr);							\
			stp_move_count++;								\
		}											\
	}												\
}

#else

#define	MSTR_STPG_ADD(MSTR1)										\
{													\
	mstr		*lcl_mstr;									\
	char		*lcl_addr;									\
													\
	GBLREF spdesc	stringpool;									\
													\
	lcl_mstr = MSTR1;										\
	if (lcl_mstr->len)										\
	{												\
		lcl_addr = lcl_mstr->addr;								\
		if (IS_PTR_IN_RANGE(lcl_addr, stringpool.base, stringpool.free))	/* BYPASSOK */	\
			MSTR_STPG_PUT(lcl_mstr);							\
	}												\
}

#endif

#define PROCESS_CACHE_ENTRY(cp)							\
        if (cp->src.str.len)	/* entry is used */				\
		MSTR_STPG_ADD(&cp->src.str);					\
	/* Run list of mvals for each code stream that exists */		\
	if (cp->obj.len)							\
	{									\
		ihdr = (ihdtyp *)cp->obj.addr;					\
		fixup_cnt = ihdr->fixup_vals_num;				\
		if (fixup_cnt)							\
		{								\
			m = (mval *)((char *)ihdr + ihdr->fixup_vals_off);	\
			for (mtop = m + fixup_cnt; m < mtop; m++)		\
				MVAL_STPG_ADD(m);				\
		}								\
		fixup_cnt = ihdr->vartab_len;					\
		if (fixup_cnt)							\
		{								\
			vent = (var_tabent *)((char *)ihdr + ihdr->vartab_off);	\
			for (vartop = vent + fixup_cnt; vent < vartop; vent++)	\
				MSTR_STPG_ADD(&vent->var_name);			\
		}								\
	}

#define PROCESS_CONTIGUOUS_BLOCK(begaddr, endaddr, cstr, delta)						\
{													\
	padlen = 0;											\
	for (; cstr < topstr; cstr++)									\
	{ 	/* Note having same mstr in array more than once can cause following assert to fail */	\
		assert((*cstr)->addr >= (char *)begaddr);						\
		if (((*cstr)->addr > (char *)endaddr) && ((*cstr)->addr != (char *)endaddr + padlen))	\
			break;										\
		tmpaddr = (unsigned char *)(*cstr)->addr + (*cstr)->len;				\
		if (tmpaddr > endaddr)									\
			endaddr = tmpaddr;								\
		padlen = mstr_native_align ? PADLEN((*cstr)->len, NATIVE_WSIZE) : 0;			\
		(*cstr)->addr -= delta;									\
	}												\
}

#define COPY2STPOOL(cstr, topstr)										\
{														\
	while (cstr < topstr)											\
	{													\
		if (mstr_native_align)										\
			stringpool.free = (unsigned char *)ROUND_UP2((INTPTR_T)stringpool.free, NATIVE_WSIZE);	\
		/* Determine extent of next contiguous block and copy it into new stringpool.base. */		\
		begaddr = endaddr = (unsigned char *)((*cstr)->addr);						\
		delta = (*cstr)->addr - (char *)stringpool.free;						\
		PROCESS_CONTIGUOUS_BLOCK(begaddr, endaddr, cstr, delta);					\
		blklen = endaddr - begaddr;									\
		memcpy(stringpool.free, begaddr, blklen);							\
		stringpool.free += blklen;									\
	}													\
}

#define MOVE_WITHIN_STPOOL(cstr, topstr)									\
{														\
	while (cstr < topstr)											\
	{													\
		/* Determine extent of next contiguous block to move and move it. */				\
		if (mstr_native_align)										\
			stringpool.free = (unsigned char *)ROUND_UP2((INTPTR_T)stringpool.free, NATIVE_WSIZE);	\
		begaddr = endaddr = (unsigned char *)((*cstr)->addr);						\
		delta = (*cstr)->addr - (char *)stringpool.free;						\
		PROCESS_CONTIGUOUS_BLOCK(begaddr, endaddr, cstr, delta);					\
		blklen = endaddr - begaddr;									\
		if (delta)											\
			memmove(stringpool.free, begaddr, blklen);						\
		stringpool.free += blklen;									\
	}													\
}

#define LVZWRITE_BLOCK_GC(LVZWRITE_BLOCK)									\
{														\
	if ((NULL != (LVZWRITE_BLOCK)) && ((LVZWRITE_BLOCK)->curr_subsc))					\
	{													\
		assert((LVZWRITE_BLOCK)->sub);									\
		zwr_sub = (zwr_sub_lst *)(LVZWRITE_BLOCK)->sub;							\
		for (index = 0; index < (int)(LVZWRITE_BLOCK)->curr_subsc; index++)				\
		{												\
			assert((zwr_sub->subsc_list[index].actual != zwr_sub->subsc_list[index].first)		\
			    || !zwr_sub->subsc_list[index].second);						\
			/* we cannot garbage collect duplicate mval pointers.					\
			 * So make sure zwr_sub->subsc_list[index].actual is not pointing to an			\
			 * existing (mval *) which  is already protected					\
			 */											\
			if (zwr_sub->subsc_list[index].actual							\
			    && (zwr_sub->subsc_list[index].actual != zwr_sub->subsc_list[index].first))		\
			{											\
				MVAL_STPG_ADD(zwr_sub->subsc_list[index].actual);				\
			}											\
		}												\
	}													\
}

#define ZWRHTAB_GC(ZWRHTAB)											\
{														\
	if ((ZWRHTAB))												\
	{													\
		for (tabent_addr = (ZWRHTAB)->h_zwrtab.base, topent_addr = (ZWRHTAB)->h_zwrtab.top;		\
		     tabent_addr < topent_addr; tabent_addr++)							\
		{												\
			if (HTENT_VALID_ADDR(tabent_addr, zwr_alias_var, zav))					\
			{											\
				x = &zav->zwr_var;								\
				/* Protect MSTRs we are using to process the zwrite */				\
				assert(x->len); /* if it is valid it should have a non-zero length */		\
				MSTR_STPG_ADD(x);								\
			}											\
		}												\
	}													\
}

/* Macro used intermittently in code to debug string pool garbage collection. Set to 1 to enable */
#if 0
/* Debug FPRINTF with pre and post requisite flushing of appropriate streams  */
#define DBGSTPGCOL(x) DBGFPF(x)
#else
#define DBGSTPGCOL(x)
#endif
static void expand_stp(size_t new_size)	/* BYPASSOK */
{
	if (retry_if_expansion_fails)
		ESTABLISH(stp_gcol_ch);
	assert(IS_GTM_IMAGE || IS_MUPIP_IMAGE);
	assert(!stringpool_unexpandable);
	DBGSTPGCOL((stderr, "expand_stp(new_size=%u)\n", new_size));
	stp_init(new_size);
	if (retry_if_expansion_fails)
		REVERT;
	return;
}

#ifndef STP_MOVE
#ifdef DEBUG
/* Verify the current symbol table that we will be processing later in "stp_gcol". This version is callable from anywhere
 * and is a great debuging tool for corrupted mstrs in local variable trees. Uncomment the call near the top of stp_gcol to
 * verify it will catch what you want it to catch and sprinkle calls around other places as necessary. Be warned, this can
 * *really* slow things down so use judiciously. SE 10/2007
 */
void stp_vfy_mval(void)
{
	lv_blk		*lv_blk_ptr;
	lv_val		*lvp, *lvlimit;
	mstr		*x;
	mval		*mv;
	char		*addr;
	symval		*sym;
	symval		*symtab;

	for (symtab = curr_symval; NULL != symtab; symtab = symtab->last_tab)
	{
		for (lv_blk_ptr = symtab->lv_first_block; lv_blk_ptr; lv_blk_ptr = lv_blk_ptr->next)
		{
			for (lvp = (lv_val *)LV_BLK_GET_BASE(lv_blk_ptr), lvlimit = LV_BLK_GET_FREE(lv_blk_ptr, lvp);
				lvp < lvlimit; lvp++)
			{
				sym = LV_SYMVAL(lvp);
				if (NULL == sym)
					continue;
				assert(SYM_IS_SYMVAL(sym) && (sym == symtab));
				mv = &lvp->v;
				if (MV_IS_STRING(mv))
				{
					x = &mv->str;
					if (x->len)
					{
						addr = x->addr;
						if (IS_PTR_IN_RANGE(addr, stringpool.base, stringpool.free))	/* BYPASSOK */
							assert(0 < (x)->len);
					}
				}
				assert(LV_IS_BASE_VAR(lvp));
				assert(lvTreeIsWellFormed(LV_CHILD(lvp)));
			}
		}
	}
}

boolean_t is_stp_space_available(ssize_t space_needed)
{
	/* Asserts added here are tested every time any module in the codebase does stringpool space checks */
	assert(!stringpool_unusable);
	return IS_STP_SPACE_AVAILABLE_PRO(space_needed);
}

#endif  /* DEBUG */

void mv_parse_tree_collect(mvar *node);

void mv_parse_tree_collect(mvar *node)
{
	int stp_put_int;

	MSTR_STPG_ADD(&node->mvname);
	if (node->lson)
		mv_parse_tree_collect(node->lson);
	if (node->rson)
		mv_parse_tree_collect(node->rson);
}
#endif /* #ifndef STP_MOVE */

#ifdef STP_MOVE
/* garbage collect and move range [from,to) to stringpool adjusting all mvals/mstrs pointing in this range */
void stp_move(char *stp_move_from, char *stp_move_to)
#else
/* garbage collect and create enough space for space_asked bytes */
void stp_gcol(size_t space_asked)	/* BYPASSOK */
#endif
{
#	ifdef STP_MOVE
	int			space_asked = 0, stp_move_count = 0;
#	endif
	unsigned char		*strpool_base, *straddr, *tmpaddr, *begaddr, *endaddr;
	int			index, fixup_cnt;
	ssize_t			space_before_compact, space_after_compact, blklen, delta, space_reclaim, padlen;
	ssize_t			totspace, space_needed, tmplen, tmplen2;
	io_log_name		*l;		/* logical name pointer		*/
	lv_blk			*lv_blk_ptr;
	lv_val			*lvp, *lvlimit;
	lvTreeNode		*node, *node_limit;
	mstr			**cstr, *x, **cstr_top, *mstrp, *mstrp_top;
	mv_stent		*mvs;
	mval			*m, **mm, **mmtop, *mtop;
	intszofptr_t		lv_subs;
	stack_frame		*sf;
	tp_frame		*tf;
	zwr_sub_lst		*zwr_sub;
	ihdtyp			*ihdr;
	int			*low_reclaim_passes;
	int			*incr_factor;
	int			killcnt;
	ssize_t			stp_incr;
	ht_ent_objcode 		*tabent_objcode, *topent;
	ht_ent_mname		*tabent_mname, *topent_mname;
	ht_ent_addr		*tabent_addr, *topent_addr;
	ht_ent_str		*tabent_lit, *topent_lit;
	mliteral		*mlit;
	zwr_alias_var		*zav;
	cache_entry		*cp;
	var_tabent		*vent, *vartop;
	symval			*symtab;
	lv_xnew_var		*xnewvar;
	lvzwrite_datablk	*lvzwrblk;
	tp_var			*restore_ent;
	boolean_t		non_mandatory_expansion, exp_gt_spc_needed;
	routine_source		*rsptr;
	glvn_pool_entry		*slot, *top;
	int			i, n;
	unsigned char		*old_free;
	d_rm_struct		*rm_ptr;
	UNICODE_ONLY(utfcgr	*utfcgrp;)
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	/* Asserts added here are tested every time any module in the codebase does stringpool garbage collection */
	assert(!stringpool_unusable);
	assert(!stringpool_unexpandable);
#	ifndef STP_MOVE
	/* Before we get cooking with our stringpool GC, check if it is appropriate to call lv_val garbage collection.
	 * This is data that can get orphaned with no way to access it when aliases are used. This form of GC is only done
	 * if aliases are actively being used. It is not called with every stringpool garbage collection but every "N"
	 * calls to this routine (when GCing the runtime stringpool only). The value of N is self-tuning to some extent.
	 * There are mins and maximums to be observed.
	 */
	if ((stringpool.base == rts_stringpool.base) && (NULL != curr_symval) && curr_symval->alias_activity)
	{	/* We have alias stuff going on so see if we need a GC */
		++SPGC_since_LVGC;
		DBGRFCT((stderr, "stp_gcol: Current interval: %d  Intervals till LVGC: %d\n",
			 SPGC_since_LVGC, MAX(0, LVGC_interval - SPGC_since_LVGC)));
		if (suspend_lvgcol)
		{
			DBGRFCT((stderr, "stp_gcol: Bypassing LVGC check due to suspend_lvgcol\n"));
		} else if (SPGC_since_LVGC >= LVGC_interval)
		{	/* Time for a GC */
			killcnt = als_lvval_gc();
			DBGRFCT((stderr, "stp_gcol: Previous interval: %d ", LVGC_interval));
			if (0 == killcnt)
				/* Nothing recovered .. be less aggresive */
				LVGC_interval = MIN((LVGC_interval + 1), MAX_SPGC_PER_LVGC);
			else
				/* Some data was recovered, be more aggresive */
				LVGC_interval = MAX((LVGC_interval - 5), MIN_SPGC_PER_LVGC);
			DBGRFCT((stderr, " New interval: %d\n", LVGC_interval));
			/* SPGC_since_LVGC is cleared by als_lv_gc() */
		}
	}
#	ifdef DEBUG_REFCNT
	else if ((stringpool.base == rts_stringpool.base) && (NULL != curr_symval))
	{
		DBGRFCT((stderr, "stp_gcol: lvgcol check bypassed for lack of aliasness\n"));
	}
#	endif	/* DEBUG_REFCNT */
	if (IS_LVMON_ACTIVE && (stringpool.base == rts_stringpool.base))
		lvmon_pull_values(1);			/* Pull set of values into index 1 if rts stringpool */
#	endif	/* !STP_MOVE */
	assert(stringpool.free >= stringpool.base);
	assert(stringpool.free <= stringpool.top);
	assert(stringpool.invokestpgcollevel <= stringpool.top);
        assert(CHK_BOUNDARY_ALIGNMENT(stringpool.top) == 0);
	/* stp_vfy_mval(); / * uncomment to debug lv corruption issues.. */
#	ifdef STP_MOVE
	assert(stp_move_from < stp_move_to); /* why did we call with zero length range, or a bad range? */
	/* Assert that range to be moved does not intersect with stringpool range. */
	assert(((stp_move_from < (char *)stringpool.base) && (stp_move_to < (char *)stringpool.base))
		|| ((stp_move_from >= (char *)stringpool.top) && (stp_move_to >= (char *)stringpool.top)));
#	endif
	space_needed = ROUND_UP2(space_asked, NATIVE_WSIZE);
	assert(0 == (INTPTR_T)stringpool.base % NATIVE_WSIZE);
	if (stringpool.base == rts_stringpool.base)
	{
		low_reclaim_passes = &rts_stp_low_reclaim_passes;
		incr_factor = &rts_stp_incr_factor;
	} else if (stringpool.base == indr_stringpool.base)
	{
		low_reclaim_passes = &indr_stp_low_reclaim_passes;
		incr_factor = &indr_stp_incr_factor;
	} else
		assertpro(FALSE && stringpool.base);	/* neither rts_stringpool, nor indr_stringpool */
	if (NULL == stp_array)
		stp_array = (mstr **)malloc((stp_array_size = STP_MAXITEMS) * SIZEOF(mstr *));
	topstr = array = stp_array;
	arraytop = topstr + stp_array_size;
	/* If dqloop == 0 then we got here from mcompile. If literal_chain.que.fl == 0 then put_lit was never
	 * done as is true in gtcm_server. Test for cache_table.size is to check that we have not done a
	 * cache_init() which would be true if doing mumps standalone compile.
	 */
	if (((stringpool.base != rts_stringpool.base) || (0 == cache_table.size)))
	{
#		ifndef STP_MOVE
		if (NULL != literal_chain.que.fl)
		{	/* If hashtable exists, pull it all from there rather that searching for it twice */
			if (complits_hashtab && complits_hashtab->base)
			{
				for (tabent_lit = complits_hashtab->base, topent_lit = complits_hashtab->top;
				     tabent_lit < topent_lit; tabent_lit++)
				{
					if (HTENT_VALID_STR(tabent_lit, mliteral, mlit))
					{	/* Key first, then value */
						MSTR_STPG_ADD(&(tabent_lit->key.str));
						MVAL_STPG_ADD(&(mlit->v));
					}
				}
			} else
			{	/* No hash table, just the values */
				dqloop(&literal_chain, que, mlit)
				{
					MVAL_STPG_ADD(&(mlit->v));
				}
			}
		}
		assert(offsetof(mvar, mvname) == offsetof(mlabel, mvname));
		if (NULL != mvartab)
			mv_parse_tree_collect(mvartab);
		if (NULL != mlabtab)
			mv_parse_tree_collect((mvar *)mlabtab);
		MVAL_STPG_ADD(&(TREF(director_mval)));
		MVAL_STPG_ADD(&(TREF(window_mval)));
		MVAL_STPG_ADD(&(TREF(indirection_mval)));
#		endif
	} else
	{
#		ifndef STP_MOVE
		/* Some house keeping since we are garbage collecting. Clear out all the lookaside
		 * arrays for the simple $piece function.
		 */
#		ifdef DEBUG
		GBLREF int c_clear;
		++c_clear;		/* Count clearing operations */
#		endif
		for (index = 0; FNPC_MAX > index; index++)
		{
			(TREF(fnpca)).fnpcs[index].last_str.addr = NULL;
			(TREF(fnpca)).fnpcs[index].last_str.len = 0;
			(TREF(fnpca)).fnpcs[index].delim = 0;
		}
#		ifdef UNICODE_SUPPORTED
		for (utfcgrp = (TREF(utfcgra)).utfcgrs; (NULL != utfcgrp) && (utfcgrp <= (TREF(utfcgra)).utfcgrmax);
		     utfcgrp = (utfcgr *)((UINTPTR_T)utfcgrp + (TREF(utfcgra)).utfcgrsize))
		{	/* Clear string addresses for each cache field since they are likely to change with the
			 * this garbage collection.
			 */
			utfcgrp->last_str.addr = NULL;
			utfcgrp->last_str.len = 0;
			utfcgrp->reference = FALSE;
		}
#		endif
#		endif
		assert(0 != cache_table.size);	/* Must have done a cache_init() */
		/* These cache entries have mvals in them we need to keep */
		for (tabent_objcode = cache_table.base, topent = cache_table.top; tabent_objcode < topent; tabent_objcode++)
		{
			if (HTENT_VALID_OBJCODE(tabent_objcode, cache_entry, cp))
			{
				MSTR_STPG_ADD(&(tabent_objcode->key.str));
				PROCESS_CACHE_ENTRY(cp);
			}
		}
		for (symtab = curr_symval; NULL != symtab; symtab = symtab->last_tab)
		{
			for (tabent_mname = symtab->h_symtab.base, topent_mname = symtab->h_symtab.top;
			     tabent_mname < topent_mname; tabent_mname++)
			{
				if (!HTENT_EMPTY_MNAME(tabent_mname, lv_val, lvp))
				{	/* Note this code checks for "not empty" rather than VALID hash table entry
					 * because a deleted entry still has a key that needs to be preserved. With
					 * the introduction of aliases, the $ZWRTAC* pseudo local vars are created and
					 * deleted where before aliases no deletion of MNAME entries ever occurred.
					 */
					MSTR_STPG_ADD(&(tabent_mname->key.var_name));
				}
			}
			for (xnewvar = symtab->xnew_var_list; xnewvar; xnewvar = xnewvar->next)
				MSTR_STPG_ADD(&xnewvar->key.var_name);
		}
		if (x = comline_base)
			for (index = MAX_RECALL; index > 0 && x->len; index--, x++)
			{	/* These strings are guaranteed to be in the stringpool so use MSTR_STPG_PUT macro directly
				 * instead of going through MSTR_STPG_ADD. But assert accordingly to be sure.
				 */
				assert(IS_PTR_IN_RANGE(x->addr, stringpool.base, stringpool.free));
				MSTR_STPG_PUT(x);
			}
		for (lvzwrblk = lvzwrite_block; NULL != lvzwrblk; lvzwrblk = lvzwrblk->prev)
		{
			LVZWRITE_BLOCK_GC(lvzwrblk);
		}
		ZWRHTAB_GC(zwrhtab);
		for (l = io_root_log_name; NULL != l; l = l->next)
		{
			if ((NULL == l->iod) || (n_io_dev_types == l->iod->type))
			{
				assert(FALSE);
				continue;       /* skip it on pro */
			}
			if ((IO_ESC != l->dollar_io[0]) && (l->iod->trans_name == l))
			{
				MSTR_STPG_ADD(&l->iod->error_handler);
				/* If this is a split $principal, protect error_handler defined on the output side */
				if ((l->iod->pair.in != l->iod->pair.out)
				    && (l->iod->pair.out == io_std_device.out))
					MSTR_STPG_ADD(&l->iod->pair.out->error_handler);
				rm_ptr = (rm == l->iod->type) ? (d_rm_struct *)l->iod->dev_sp : NULL;
				if (NULL != rm_ptr)
				{	/* Protect the IVs and KEYs as needed. */
					if (rm_ptr->input_encrypted)
					{
						MSTR_STPG_ADD(&rm_ptr->input_iv);
						MSTR_STPG_ADD(&rm_ptr->input_key);
					}
					if (rm_ptr->output_encrypted)
					{
						MSTR_STPG_ADD(&rm_ptr->output_iv);
						MSTR_STPG_ADD(&rm_ptr->output_key);
					}
				}
			}
		}
		MVAL_STPG_ADD(&(TREF(dollar_etrap)));
		MVAL_STPG_ADD(&dollar_system);
		MVAL_STPG_ADD(&dollar_zsource);
		MVAL_STPG_ADD(&(TREF(dollar_ztrap)));
		MVAL_STPG_ADD(&dollar_zstatus);
		MVAL_STPG_ADD(&dollar_zgbldir);
		MVAL_STPG_ADD(&dollar_zinterrupt);
		MVAL_STPG_ADD(&(TREF(dollar_zstep)));
		MVAL_STPG_ADD(&zstep_action);
		MVAL_STPG_ADD(&dollar_zerror);
		MVAL_STPG_ADD(&dollar_ztexit);
		MVAL_STPG_ADD(&dollar_zyerror);
#		ifdef GTM_TRIGGER
		MVAL_STPG_ADD(&dollar_ztwormhole);
#		endif
		MVAL_STPG_ADD(TADR(last_fnquery_return_varname));
		for (index = 0; index < TREF(last_fnquery_return_subcnt); index++)
			MVAL_STPG_ADD(&TAREF1(last_fnquery_return_sub, index));
		for (mvs = mv_chain; mvs < (mv_stent *)stackbase; mvs = (mv_stent *)((char *)mvs + mvs->mv_st_next))
		{
			switch (mvs->mv_st_type)
			{
				case MVST_MVAL:
					m = &mvs->mv_st_cont.mvs_mval;
					break;
				case MVST_MSAV:
					m = &mvs->mv_st_cont.mvs_msav.v;
					break;
				case MVST_STAB:
					if (NULL != (symtab = mvs->mv_st_cont.mvs_stab))
					{	/* if initalization of the table was successful */
						for (lv_blk_ptr = symtab->lv_first_block; NULL != lv_blk_ptr;
						     lv_blk_ptr = lv_blk_ptr->next)
						{
							for (lvp = (lv_val *)LV_BLK_GET_BASE(lv_blk_ptr),
							    lvlimit = LV_BLK_GET_FREE(lv_blk_ptr, lvp);
							    lvp < lvlimit; lvp++)
							{
								/* lvp could be actively in use or free (added to the symval's
								 * lv_flist). Ignore the free ones. Those should have their
								 * "parent" (aka "symval" since this is a lv_val) set to NULL.
								 */
								if (NULL == LV_PARENT(lvp))
									continue;
								assert(LV_IS_BASE_VAR(lvp));
								/* For a base variable, we need to protect only the node value
								 * (if it exists and is of type string).
								 */
								MVAL_STPG_ADD(&(lvp->v));
							}
						}
						for (lv_blk_ptr = symtab->lvtreenode_first_block; NULL != lv_blk_ptr;
						     lv_blk_ptr = lv_blk_ptr->next)
						{
							for (node = (lvTreeNode *)LV_BLK_GET_BASE(lv_blk_ptr),
								     node_limit = LV_BLK_GET_FREE(lv_blk_ptr, node);
							     node < node_limit; node++)
							{
								/* node could be actively in use or free (added to the symval's
								 * lvtreenode_flist). Ignore the free ones. Those should have
								 * their "parent"  set to NULL.
								 */
								if (NULL == LV_PARENT(node))
									continue;
								assert(!LV_IS_BASE_VAR(node));
								/* For a subscripted variable, we need to protect the subscript
								 * (if string) as well as the subscripted node value (if exists
								 * and is of type string).
								 */
								LV_NODE_KEY_STPG_ADD(node);	/* Protect node subscript */
								MVAL_STPG_ADD(&(node->v));	/* Protect node value */
							}
						}
					}
					continue;	/* continue loop, not after switch stmt */
				case MVST_IARR:
					m = (mval *)mvs->mv_st_cont.mvs_iarr.iarr_base;
					for (mtop = m + mvs->mv_st_cont.mvs_iarr.iarr_mvals; m < mtop; m++)
						MVAL_STPG_ADD(m);
					continue;
				case MVST_LVAL:
				case MVST_NTAB:
				case MVST_TVAL:
				case MVST_STCK:
				case MVST_STCK_SP:
				case MVST_PVAL:
				case MVST_RSTRTPC:
				case MVST_STORIG:
				case MVST_ZINTCMD:
					continue;
				case MVST_TPHOLD:
#					ifdef GTM_TRIGGER
					if (0 == mvs->mv_st_cont.mvs_tp_holder.tphold_tlevel)
					{	/* $ZTWORMHOLE only saved at initial TP level */
						MVAL_STPG_ADD(&mvs->mv_st_cont.mvs_tp_holder.ztwormhole_save);
					}
#					endif
					continue;
				case MVST_NVAL:
					/* The var_name field is only present in a debug build */
					DEBUG_ONLY(MSTR_STPG_ADD(&mvs->mv_st_cont.mvs_nval.name.var_name));
					continue;
#				ifdef GTM_TRIGGER
				case MVST_TRIGR:
					/* Top elements of MVST_TRIGR and MVST_ZINTR structures are the same. These common
					 * elements are processed when we fall through to the MVST_ZINTR entry below. Here
					 * we process the unique MVST_TRIGR elements.
					 */
					MVAL_STPG_ADD(&mvs->mv_st_cont.mvs_trigr.dollar_etrap_save);
					MVAL_STPG_ADD(&mvs->mv_st_cont.mvs_trigr.dollar_ztrap_save);
					/* Note fall into MVST_ZINTR to process common entries */
#				endif
				case MVST_ZINTR:
					MSTR_STPG_ADD(&mvs->mv_st_cont.mvs_zintr.savextref);
					m = &mvs->mv_st_cont.mvs_zintr.savtarg;
					break;
				case MVST_ZINTDEV:
					if (mvs->mv_st_cont.mvs_zintdev.buffer_valid)
						MSTR_STPG_ADD(&mvs->mv_st_cont.mvs_zintdev.curr_sp_buffer);
					continue;
				case MVST_MRGZWRSV:
					LVZWRITE_BLOCK_GC(mvs->mv_st_cont.mvs_mrgzwrsv.save_lvzwrite_block);
					ZWRHTAB_GC(mvs->mv_st_cont.mvs_mrgzwrsv.save_zwrhtab);
					continue;
				default:
					assertpro(FALSE && mvs->mv_st_type);
			}
			MVAL_STPG_ADD(m);
		}
		/* If LINK:RECURSIVE is enabled and the first M frame on the stack is recursively replaced, when it unwinds,
		 * op_unwind() is going to unlink that version of the routine. We will eventually come here (as stp_move)
		 * with a zeroed frame pointer so the test below needs to deal with a NULL frame_pointer value.
		 */
		if (NULL != frame_pointer)
		{
			for (sf = frame_pointer; sf && (sf < (stack_frame *)stackbase); sf = sf->old_frame_pointer)
			{	/* Move temp mvals in use to stringpool
				 *
				 * While running through the stack, we need to skip over base frames so we process the
				 * entire stack with the following caveats:
				 *   1. The original base frame (has a type of SFT_COUNT but is otherwise unmarked) is the
				 *	absolute bottom of the stack so breaks the loop. This will only be seen when the
				 *	base frame (for execution) is a call-in frame either for simpleAPI or for call-ins.
				 *   2. When running mumps, a different base frame, similar in construction to the original
				 *	base frame described above, that also has a type of SFT_COUNT should stop rearward
				 *	stack travel and break the loop.
				 */
				SKIP_BASE_FRAMES(sf);		/* Updates sf */
				if (NULL == sf)
					break;
				assert(sf->temps_ptr);
				if (sf->temps_ptr >= (unsigned char *)sf)
					continue;
				m = (mval *)sf->temps_ptr;
				for (mtop = m + sf->temp_mvals; m < mtop; m++)
				{	/* DM frames should not normally have temps. If they do then it better have mvtype 0
					 * thereby guaranteeing it will not need stp_gcol protection by the MVAL_STPG_ADD macro
					 * below.
					 */
					assert(!(sf->type & SFT_DM) || !MV_DEFINED(m));
					MVAL_STPG_ADD(m);
				}
			}
		}
		if (NULL != TREF(glvn_pool_ptr))
		{	/* Protect everything in the glvn pool. */
			m = (TREF(glvn_pool_ptr))->mval_stack;
			for (mtop = m + (TREF(glvn_pool_ptr))->mval_top; m < mtop; m++)
				MVAL_STPG_ADD(m);
		}
		if (NULL != alias_retarg)
		{	/* An alias return value is in-flight - process it */
			assert(alias_retarg->mvtype & MV_ALIASCONT);
			if (alias_retarg->mvtype & MV_ALIASCONT)
			{	/* Protect the refs were are about to make in case ptr got banged up somehow */
				lvp = (lv_val *)alias_retarg;
				assert(LV_IS_BASE_VAR(lvp));
				MVAL_STPG_ADD(&lvp->v);
			}
		}
		if (tp_pointer)
		{
			tf = tp_pointer;
			while (tf)
			{
				MVAL_STPG_ADD(&tf->trans_id);
				MVAL_STPG_ADD(&tf->zgbldir);
				for (restore_ent = tf->vars; restore_ent; restore_ent = restore_ent->next)
					MSTR_STPG_ADD(&(restore_ent->key.var_name));
				tf = tf->old_tp_frame;
			}
		}
		/* Check for mstrs being used by the simple API */
		if (0 < TREF(sapi_mstrs_for_gc_indx))
		{	/* The simpleAPI has some mstrs in use it needs protected. Add the array's addresses to
			 * our own array of mstrs.
			 */
			for (cstr = TADR(sapi_mstrs_for_gc_ary), cstr_top = cstr + TREF(sapi_mstrs_for_gc_indx);
			     cstr < cstr_top;
			     cstr++)
			{
				MSTR_STPG_PUT(*cstr);
			}
		}
		if (0 < TREF(sapi_query_node_subs_cnt))
		{	/* Another set of mstrs used to return subscripts from ydb_node_{next,previous}_s() */
			for (mstrp = TREF(sapi_query_node_subs), mstrp_top = mstrp + TREF(sapi_query_node_subs_cnt);
			     mstrp < mstrp_top;
			     mstrp++)
			{
				if (0 < mstrp->len)
					MSTR_STPG_PUT(mstrp);
			}
		}
	}
	space_before_compact = stringpool.top - stringpool.free; /* Available space before compaction */
	DEBUG_ONLY(blklen = stringpool.free - stringpool.base);
	old_free = stringpool.free;
	/* Reset stringpool.free in the hope stringpool garbage collection (and expansion if needed) would happen.
	 * If there are any errors in this process, we need to remember to restore stringpool.free as otherwise
	 * any mval in the stringpool could potentially be overwritten later (for a new mval since there is an
	 * incorrect view of available stringpool space) resulting in memory corruption.
	 */
	stringpool.free = stringpool.base;
	if (topstr != array)
	{
		stpg_sort(array, topstr - 1);
		for (totspace = 0, cstr = array, straddr = (unsigned char *)(*cstr)->addr; (cstr < topstr); cstr++ )
		{
			assert((cstr == array) || ((*cstr)->addr >= ((*(cstr - 1))->addr)));
			tmpaddr = (unsigned char *)(*cstr)->addr;
			tmplen = (*cstr)->len;
			assert(0 < tmplen);
			if (tmpaddr + tmplen > straddr) /* if it is not a proper substring of previous one */
			{
				tmplen2 = ((tmpaddr >= straddr) ? tmplen : (ssize_t)(tmpaddr + tmplen - straddr));
				assert(0 < tmplen2);
				totspace += tmplen2;
				if (mstr_native_align)
					totspace += PADLEN(totspace, NATIVE_WSIZE);
				straddr = tmpaddr + tmplen;
			}
		}
		/* Now totspace is the total space needed for all the current entries and any stp_move entries.
		 * Note that because of not doing exact calculation with substring, totspace may be little more
		 * than what is needed.
		 */
		space_after_compact = stringpool.top - stringpool.base - totspace; /* can be -ve number */
	} else
		space_after_compact = stringpool.top - stringpool.free;
#	ifndef STP_MOVE
	assert(mstr_native_align || space_after_compact >= space_before_compact);
#	endif
	space_reclaim = space_after_compact - space_before_compact; /* this can be -ve, if alignment causes expansion */
	space_needed -= (ssize_t)space_after_compact;
	DBGSTPGCOL((stderr, "space_needed=%i\n", space_needed));
	/* After compaction if less than 31.25% of space is avail, consider it a low reclaim pass */
	if (STP_LOWRECLAIM_LEVEL(stringpool.top - stringpool.base) > space_after_compact) /* BYPASSOK */
		(*low_reclaim_passes)++;
	else
		*low_reclaim_passes = 0;
	non_mandatory_expansion = FALSE;
	if (0 < space_needed /* i */
#	ifndef STP_MOVE /* do expansion only for stp_gcol, no forced expansion for stp_move */
	    || (non_mandatory_expansion =
	        (!stop_non_mandatory_expansion && /* ii */
	         (STP_MAXLOWRECLAIM_PASSES <= *low_reclaim_passes ))) /* iii */
#	endif
		)
	{	/* i   - more space needed than available so do a mandatory expansion
		 * ii  - non-mandatory expansions disabled because a previous attempt failed due to lack of memory
		 * iii - after compaction, if at least 31.25% of string pool is not free for STP_MAXLOWRECLAIM_PASSES
		 *       do a non-mandatory expansion. This is done to avoid scenarios where there is repeated
		 * 	     occurrences of the stringpool filling up, and compaction not reclaiming enough space.
		 *       In such cases, if the stringpool is expanded, we create more room, resulting in fewer calls
		 *       to stp_gcol.
		 *
		 * Note it is not uncommon for space_needed to be a negative value. That can occur in here if we
		 * recovered enough space from the GC to satisfy the immediate need but we have identified that
		 * so little space is still available that we are best served by expanding the stringpool so as to
		 * prevent another almost immediate need for another GC. This is also known as a non-mandatory
		 * expansion. Assert that if space_needed is negative, we have a non-mandatory expansion.
		 */
		assert((0 <= space_needed) || non_mandatory_expansion);
		strpool_base = stringpool.base;
		/* Grow stringpool geometrically */
		stp_incr = (stringpool.top - stringpool.base) * *incr_factor / STP_NUM_INCRS;
		do
		{
			if (stp_incr < space_needed)
				stp_incr = space_needed;
			/* If we are asking for more than is actually needed we want to try again if we do not get it. */
			retry_if_expansion_fails = (stp_incr > space_needed);
			/* If this is a non-mandatory expansion (which also means space_needed is likely negative but this is
			 * not *always* the case) and stp_incr has come down to less than the size of a page, then there's really
			 * no point in continuing this loop - probably all the way to stp_incr = 0. But for a non-mandatory
			 * expansion, leaving early carries no significiant penalty - especially since we're already operating
			 * on the the edge.
			 */
			if (non_mandatory_expansion && (0 == stp_incr))
				break;			/* stp_incr can't get smaller - give up and use what we have */
			expansion_failed = FALSE;	/* will be set to TRUE by condition handler if can't get memory */
			assert((stp_incr + stringpool.top - stringpool.base) >= (space_needed + blklen));
			DBGSTPGCOL((stderr, "incr_factor=%i stp_incr=%i space_needed=%i\n", *incr_factor, stp_incr, space_needed));
			if ((TREF(gtm_strpllimwarned)) /* previously warned */
				&& (0 < TREF(gtm_strpllim)) /* watching a stp limit */
				&& ((stp_incr + stringpool.top - stringpool.base) > TREF(gtm_strpllim))) /* expanding larger */
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_STPOFLOW);
			expand_stp((ssize_t)(stp_incr + stringpool.top - stringpool.base));
#			ifdef DEBUG
			/* If expansion failed and stp_gcol_ch did an UNWIND and we were already in exit handling code,
			 * ok_to_UNWIND_in_exit_handling would have been set to avoid an assert in UNWIND macro. Now that
			 * we are back from stp_gcol_ch, reset it so future UNWINDs in exit-handling are assert checked.
			 */
			if (expansion_failed && process_exiting)
				ok_to_UNWIND_in_exit_handling = FALSE;
#			endif
			if (!retry_if_expansion_fails || !expansion_failed)
				break;
			/* Once we hit the memory wall no more non_mandatory expansions */
			if (non_mandatory_expansion)
				stop_non_mandatory_expansion = TRUE;
			/* We were not able to get the memory we wanted. Plan B it to get as much of what we wanted
			 * (down to what was actually needed). If we can't get what was actually needed allow
			 * the user to get a memory error.
			 */
			if (1 < *incr_factor)
			{
				/* if we hit the memory wall with an elevated incr_factor drop back a notch and retry */
				*incr_factor = *incr_factor - 1;
				stp_incr = (stringpool.top - stringpool.base) * *incr_factor / STP_NUM_INCRS;
			} else
			{	/* if we are already at the lowest incr_factor half our way down */
				if (stp_incr > space_needed)
					stp_incr = stp_incr / 2;
			}
		} while (TRUE);
		cstr = array;
		if (strpool_base != stringpool.base) /* expanded successfully */
		{
			COPY2STPOOL(cstr, topstr);
			/* NOTE: rts_stringpool must be kept up-to-date because it tells whether the current
			 * stringpool is the run-time or indirection stringpool.
		 	 */
			(strpool_base == rts_stringpool.base) ? (rts_stringpool = stringpool) : (indr_stringpool = stringpool);
			free(strpool_base);
			/* Adjust incr_factor */
			if (*incr_factor < STP_NUM_INCRS) *incr_factor = *incr_factor + 1;
		} else
		{	/* Could not expand during forced expansion */
			assert(non_mandatory_expansion && stop_non_mandatory_expansion);
			if (space_after_compact < space_needed)
			{	/* Restore stringpool.free since no garbage collection happened. */
				stringpool.free = old_free;
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_STPEXPFAIL, 1,
					      (stp_incr + stringpool.top - stringpool.base));
			}
			MOVE_WITHIN_STPOOL(cstr, topstr);
		}
		*low_reclaim_passes = 0;
	} else
	{
		assert(stringpool.free == stringpool.base);
		/* Adjust incr_factor */
		if (*incr_factor > 1)
			*incr_factor = *incr_factor - 1;
		if (topstr != array)
		{
#			ifdef STP_MOVE
			if (0 != stp_move_count)
			{	/* All stp_move elements must be contiguous in the 'array'. They point outside
				 * the range of stringpool.base and stringpool.top. In the 'array' of (mstr *) they
				 * must be either at the beginning, or at the end.
				 */
				DBGSTPGCOL((stderr, "STP_MOVE defined\n"));
				tmpaddr = (unsigned char *)(*array)->addr;
				if (IS_PTR_IN_RANGE(tmpaddr, stringpool.base, stringpool.top))	/* BYPASSOK */
					topstr -= stp_move_count;/* stringpool elements before move elements in stp_array */
				else
					array += stp_move_count;/* stringpool elements after move elements or no stringpool
								 * elements in stp_array */
			}
#			endif
			/* Skip over contiguous block, if any, at beginning of stringpool.
			 * Note that here we are not considering any stp_move() elements.
			 */
			cstr = array;
			begaddr = endaddr = (unsigned char *)((*cstr)->addr);
			MOVE_WITHIN_STPOOL(cstr, topstr);
		}
#		ifdef STP_MOVE
		if (0 != stp_move_count)
		{	/* Copy stp_move elements into stringpool now */
			assert(topstr == cstr); /* all stringpool elements garbage collected */
			if (array == stp_array) /* stringpool elements before move elements in stp_array */
				topstr += stp_move_count;
			else
			{ /* stringpool elements after move elements OR no stringpool elements in stp_array */
				cstr = stp_array;
				topstr = array;
			}
			COPY2STPOOL(cstr, topstr);
		}
#		endif
	}
	assert(stringpool.free >= stringpool.base);
	assert(stringpool.free <= stringpool.top);
	stringpool.invokestpgcollevel = (STP_SPACE_USED_MULTIPLIER * (space_asked + stringpool.free - stringpool.base))
		+ stringpool.base;
	stringpool.invokestpgcollevel = (((stringpool.invokestpgcollevel - stringpool.base) < STP_GCOL_TRIGGER_FLOOR)
		|| (stringpool.invokestpgcollevel > stringpool.top))
		? stringpool.top : stringpool.invokestpgcollevel;
	assert(stringpool.invokestpgcollevel <= stringpool.top);
#	ifndef STP_MOVE
	assert(stringpool.top - stringpool.free >= space_asked);
	if (IS_LVMON_ACTIVE && (stringpool.base == rts_stringpool.base))
	{	/* Do monitoring only for runtime stringpool */
		lvmon_pull_values(2);				/* Pull values of monitored vars into index 2 */
		lvmon_compare_value_slots(1, 2);		/* Make sure they are the same */
	}
#	endif	/* !STP_MOVE */
	if ((0 < TREF(gtm_strpllim)) /* monitoring stp limit */
		&& ((stringpool.top - stringpool.base) > TREF(gtm_strpllim))) /* past the stp limit */
	{
		TREF(gtm_strpllimwarned) =  TRUE;
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_STPCRIT);
	}
	return;
}
