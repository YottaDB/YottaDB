/****************************************************************
 *								*
 * Copyright 2001, 2004 Sanchez Computer Associates, Inc.	*
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

#include "io.h"
#include "iousdef.h"
#include "op.h"

LITDEF unsigned char spaces_block[TAB_BUF_SZ] =
{
	SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,
	SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,
	SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,
	SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,
	SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,
	SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,
	SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,
	SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP,SP
};
LITDEF unsigned char ebcdic_spaces_block[TAB_BUF_SZ] =
{
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,
	EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP,EBCDIC_SP
};

GBLREF io_pair		io_curr_device;
GBLREF io_desc		*active_device;

void op_wttab(mint col)
{
	mstr		spaces;
	int		delta;
	io_desc		*iod;

	iod = io_curr_device.out;
	iod->esc_state = START;
	if ((delta = col - iod->dollar.x) > 0)
	{
		active_device = iod;
		if ((us == iod->type) && (NULL != (((d_us_struct*)(iod->dev_sp))->disp->wttab)))
			(((d_us_struct*)(iod->dev_sp))->disp->wttab)(delta);
		else
		{
			spaces.addr = (char *)SPACES_BLOCK;
			spaces.len = TAB_BUF_SZ;
			for (; delta > 0; delta -= spaces.len)
			{
				if (delta < TAB_BUF_SZ)
					spaces.len = delta;
				(iod->disp_ptr->write)(&spaces);
			}
		}
		if (iod->wrap)
		{
			iod->dollar.x = col % iod->width;
			assert((0 == iod->length) || (iod->dollar.y < iod->length));
			iod->dollar.y += (col / iod->width);
			if (iod->length)
				iod->dollar.y %= iod->length;
		} else
			iod->dollar.x = col;
		active_device = NULL;
	}
	return;
}
