/****************************************************************
 *								*
 * Copyright (c) 2001-2012 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
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

#include "mdef.h"

#include "gtm_string.h"

#include "error.h"
#include "dm_setup.h"
#include "rtnhdr.h"
#include "op.h"
#include "compiler.h"
#include "emit_code.h"
#include "inst_flush.h"
#include "obj_file.h"
#include "gtm_text_alloc.h"
#include "make_dmode.h"

/* This routine is called to create a dynamic routines that* can be executed. There used to
 * be two modes so this variable mechanism was left in place in case one is needed in the future.
 * Currently, we only build a direct mode frame.
 */
typedef struct dyn_modes_struct
{
	char 		*rtn_name;
	int		rtn_name_len;
	void		(*func_ptr1)(void);
	void		(*func_ptr2)(void);
	int		(*func_ptr3)(void);
} dyn_modes;

static dyn_modes our_modes[1] =
{
	{
		GTM_DMOD,
		SIZEOF(GTM_DMOD) - 1,
		dm_setup,
		mum_tstart,
		opp_ret
	}
};

rhdtyp *make_dmode (void)
{
	rhdtyp		*base_address;
	lab_tabent	*lbl;
	lnr_tabent	*lnr;
	CODE_TYPE	*code;
	dyn_modes	*dmode;
	int algnd_rtnhdr_size = (int)ROUND_UP2(SIZEOF(rhdtyp), SECTION_ALIGN_BOUNDARY);
	int algnd_code_size   = (int)ROUND_UP2(CODE_SIZE, NATIVE_WSIZE);
	int algnd_lbltab_size = (int)ROUND_UP2(SIZEOF(lab_tabent), NATIVE_WSIZE);
	int algnd_lnrtab_size = (int)ROUND_UP2(CODE_LINES * SIZEOF(lnr_tabent), NATIVE_WSIZE);
	static rhdtyp	*make_dmode_base_address = NULL;

	if (NULL != make_dmode_base_address)
	{	/* "make_dmode" was already called by this process. Use the base address computed then and return right away. */
		return make_dmode_base_address;
	}
        base_address = (rhdtyp *)GTM_TEXT_ALLOC(algnd_rtnhdr_size + algnd_code_size + algnd_lbltab_size + algnd_lnrtab_size);
	memset(base_address, 0, algnd_rtnhdr_size + algnd_code_size + algnd_lbltab_size + algnd_lnrtab_size);
	dmode = &our_modes[DM_MODE];
	base_address->routine_name.len = dmode->rtn_name_len;
	base_address->routine_name.addr = dmode->rtn_name;
	base_address->ptext_adr = (unsigned char *)base_address + algnd_rtnhdr_size;
	base_address->ptext_end_adr = (unsigned char *)base_address->ptext_adr + algnd_code_size;
	base_address->lnrtab_adr = (lnr_tabent *)base_address->ptext_end_adr;
	base_address->labtab_adr = (lab_tabent *)((unsigned char *)base_address + algnd_rtnhdr_size +
						  algnd_code_size + algnd_lnrtab_size);
	base_address->lnrtab_len = CODE_LINES;
	base_address->labtab_len = 1;
	code = (CODE_TYPE *)base_address->ptext_adr;	/* start of executable code */
	GEN_CALL(dmode->func_ptr1);			/* line 0,1 */
	GEN_CALL(dmode->func_ptr2);
	GEN_CALL(dmode->func_ptr3); 			/* line 2 */
	lnr = LNRTAB_ADR(base_address);
	*lnr++ = 0;						/* line 0 */
	*lnr++ = 0;						/* line 1 */
	*lnr++ = 2 * CALL_SIZE + EXTRA_INST * SIZEOF(int);	/* line 2 */
	lbl = base_address->labtab_adr;
	lbl->lnr_adr = base_address->lnrtab_adr;
	base_address->current_rhead_adr = base_address;
	zlput_rname(base_address);
	inst_flush(base_address, algnd_rtnhdr_size + algnd_code_size + algnd_lbltab_size + algnd_lnrtab_size);
	make_dmode_base_address = base_address;	/* store for future calls to "make_dmode" (e.g. call-in, simpleAPI etc.) */
	return base_address;
}
