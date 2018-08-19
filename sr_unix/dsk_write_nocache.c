/****************************************************************
 *								*
 * Copyright (c) 2005-2017 Fidelity National Information	*
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

#include "gtm_fcntl.h"	/* needed for silly aix's expansion of open to open64 */
#include "gtm_unistd.h"
#include "gtm_stdio.h"
#include "gtm_signal.h"

#include <sys/types.h>
#include <errno.h>

#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "gdsblk.h"
#include "iosp.h"
#include "gtmio.h"
#include "gds_blk_downgrade.h"
#include "add_inter.h"
#include "anticipatory_freeze.h"
#include "gtmcrypt.h"
#include "min_max.h"
#include "jnl.h"

GBLREF	sm_uc_ptr_t	reformat_buffer;
GBLREF	int		reformat_buffer_len;
GBLREF	volatile int	reformat_buffer_in_use;	/* used only in DEBUG mode */
GBLREF	volatile int4	fast_lock_count;

/*
 * 1) We write direct from the given buffer to a block in the database file on disk rather than from a cache record's buffer.
 * 2) This routine takes care of the maint of blks_to_upgrd in the file-header for these non-cached writes.
 */
int	dsk_write_nocache(gd_region *reg, block_id blk, sm_uc_ptr_t buff, enum db_ver ondsk_blkver)
{
	unix_db_info		*udi;
	int4			size, save_errno;
	sgmnt_addrs		*csa;
	sgmnt_data_ptr_t	csd;
	int			in_len, this_blk_size, gtmcrypt_errno;
	char			*in;
	gd_segment		*seg;
	boolean_t		use_new_key;
#	ifdef DEBUG
	sm_uc_ptr_t		save_buff;
#	endif
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	udi = FILE_INFO(reg);
	csa = &udi->s_addrs;
	csd = csa->hdr;
	assert(NULL != csd);
	assert(((blk_hdr_ptr_t)buff)->bver);	/* GDSV4 (0) version uses this field as a block length so should always be > 0 */
	assert(0 == fast_lock_count); /* ensure the static reformat buffer is not being used currently */
	++fast_lock_count; 	/* Prevents interrupt from using reformat buffer while we have it */
	/* reformat_buffer_in_use should always be incremented only AFTER incrementing fast_lock_count
	 * as it is the latter that prevents interrupts from using the reformat buffer. Similarly
	 * the decrement of fast_lock_count should be done AFTER decrementing reformat_buffer_in_use.
	 */
	assert(0 == reformat_buffer_in_use);
	DEBUG_ONLY(reformat_buffer_in_use++;)
	if (IS_GDS_BLK_DOWNGRADE_NEEDED(ondsk_blkver))
	{	/* Need to downgrade/reformat this block back to the previous format */
		DEBUG_DYNGRD_ONLY(PRINTF("DSK_WRITE_NOCACHE: Block %d being dynamically downgraded on write\n", blk));
		if (csd->blk_size > reformat_buffer_len)
		{	/* Buffer not big enough (or does not exist) .. get a new one releasing old if it exists */
			if (reformat_buffer)
				free(reformat_buffer);	/* Different blksized databases in use .. keep only largest one */
			reformat_buffer = malloc(csd->blk_size);
			reformat_buffer_len = csd->blk_size;
		}
		gds_blk_downgrade((v15_blk_hdr_ptr_t)reformat_buffer, (blk_hdr_ptr_t)buff);
		buff = reformat_buffer;
		size = (((v15_blk_hdr_ptr_t)buff)->bsiz + 1) & ~1;
		/* Represents a block state change from V5 -> V4 */
		INCR_BLKS_TO_UPGRD(csa, csd, 1);
		assert(SIZEOF(v15_blk_hdr) <= size);
	} else DEBUG_ONLY(if (GDSV6 == ondsk_blkver))
	{
		size = (((blk_hdr_ptr_t)buff)->bsiz + 1) & ~1;
		assert(SIZEOF(blk_hdr) <= size);
		/* no adjustment to blks_to_upgrd counter is needed since the format we are going to write is GDSVCURR */
	}
#	ifdef DEBUG
	else
		assert(GDSV6 == ondsk_blkver);
#	endif
	if (csa->do_fullblockwrites) /* See similiar lobic in wcs_wtstart.c */
		size = (int)ROUND_UP(size, (FULL_DATABASE_WRITE == csa->do_fullblockwrites)
				? csd->blk_size : csa->fullblockwrite_len);
	assert(size <= csd->blk_size);
	assert(FALSE == reg->read_only);
	/* This function is called by "bml_init" which in turn can be called by "mucregini" or "gdsfilext". The former is
	 * a case where the region is not yet open. csa is usable to a limited extent in the former case but since shared
	 * memory is not set up yet, most of it (e.g. csa->nl etc.) are not usable in the former case. But it is usable
	 * completely in the latter case. Therefore take care using "csa" below. Hence the "reg->open" usages before csa access.
	 * The reg->open check was not necessary until statsdb support because the only caller of "mucregini" was MUPIP CREATE
	 * which would not anyways open the journal pool. But now that GT.M can call "mucregini" to create a statsdb while
	 * it has other basedbs open, it could have the jnlpool open which is why we need this safety check.
	 */
	assert(buff != (sm_uc_ptr_t)csd);
	assert(size <= csd->blk_size);
	if (udi->raw)
		size = ROUND_UP(size, DISK_BLOCK_SIZE);	/* raw I/O must be a multiple of DISK_BLOCK_SIZE */
	use_new_key = USES_NEW_KEY(csd);
	if (IS_ENCRYPTED(csd->is_encrypted) || use_new_key)
	{
		this_blk_size = ((blk_hdr_ptr_t)buff)->bsiz;
		assert((this_blk_size <= csd->blk_size) && (this_blk_size >= SIZEOF(blk_hdr)));
		in_len = MIN(csd->blk_size, this_blk_size) - SIZEOF(blk_hdr);
		/* Make sure we do not end up encrypting a zero-length record */
		if (BLK_NEEDS_ENCRYPTION(((blk_hdr_ptr_t)buff)->levl, in_len))
		{
			ASSERT_ENCRYPTION_INITIALIZED;
			in = (char *)(buff + SIZEOF(blk_hdr));
			GTMCRYPT_ENCRYPT(csa, (use_new_key ? TRUE : csd->non_null_iv),
					(use_new_key ? csa->encr_key_handle2 : csa->encr_key_handle),
					in, in_len, NULL, buff, SIZEOF(blk_hdr), gtmcrypt_errno);
			if (0 != gtmcrypt_errno)
			{
				seg = reg->dyn.addr;
				GTMCRYPT_REPORT_ERROR(gtmcrypt_errno, rts_error, seg->fname_len, seg->fname);
			}
		}
	}
	if (udi->fd_opened_with_o_direct)
	{
		assert(reg->open);	/* should be coming in through "gdsfilext" after having done a "gvcst_init" */
		/* This means dio_buff.aligned would already have been allocated to hold at least one GDS block. Use it. */
		size = ROUND_UP2(size, DIO_ALIGNSIZE(udi));
		assert(DIO_BUFF_NO_OVERFLOW((TREF(dio_buff)), size));
		assert(size <= csd->blk_size);
		memcpy((TREF(dio_buff)).aligned, buff, size);
		DEBUG_ONLY(save_buff = buff;)	/* for DBG purposes */
		buff = (sm_uc_ptr_t)(TREF(dio_buff)).aligned;
	}
	DB_LSEEKWRITE(reg->open ? csa : NULL, udi, udi->fn, udi->fd,
			(BLK_ZERO_OFF(csd->start_vbn) + (off_t)blk * csd->blk_size), buff, size, save_errno);
	DEBUG_ONLY(reformat_buffer_in_use--;)
	assert(0 == reformat_buffer_in_use);
	--fast_lock_count; 		/* reformat buffer is no longer necessary */
	assert(0 == fast_lock_count);
	if (0 != save_errno)		/* If it didn't work for whatever reason.. */
		return -1;
	return SS_NORMAL;
}
