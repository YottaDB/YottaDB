/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
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

#include "gtm_fcntl.h"
#include "gtm_unistd.h"
#include "gtm_string.h"
#include <errno.h>
#include "gtm_stat.h"
#include "gtm_stdio.h"
#include "gtm_statvfs.h"

#if defined(__MVS__)
#include "gtm_zos_io.h"
#endif
#include "parse_file.h"
#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "gdsblk.h"
#include "filestruct.h"
#include "mlkdef.h"
#include "gtmio.h"
#include "send_msg.h"
#include "is_raw_dev.h"
#include "disk_block_available.h"
#include "mucregini.h"
#include "mu_cre_file.h"
#include "gtmmsg.h"
#include "util.h"
#include "gtmdbglvl.h"
#include "anticipatory_freeze.h"
#include "gtmcrypt.h"
#include "shmpool.h"	/* Needed for the shmpool structures */
#include "jnl.h"
#include "db_write_eof_block.h"
#include "get_fs_block_size.h"
#include "gtm_permissions.h"
#include "getzposition.h"
#include "error.h"

#define BLK_SIZE (((gd_segment*)gv_cur_region->dyn.addr)->blk_size)

/* Note: CLEANUP macro is invoked by "mu_cre_file_ch" and "mu_cre_file" and hence needs to use "static" variables
 * (mu_cre_file_fd & mu_cre_file_path) instead of "local" variables.
 */
#define CLEANUP(XX)											\
MBSTART {												\
	int	rc;											\
													\
	if (cs_data)											\
		free(cs_data);										\
	if (FD_INVALID != mu_cre_file_fd)								\
		CLOSEFILE_RESET(mu_cre_file_fd, rc); /* resets "mu_cre_file_fd" to FD_INVALID */	\
	assert(NULL != mu_cre_file_path);								\
	if (EXIT_ERR == XX)										\
		UNLINK(mu_cre_file_path);								\
} MBEND

/* Macros to send warning or error messages to the correct destination:
 *  - If MUPIP image, message goes to stderr of the process.
 *  - Else MUMPS image captures the error message and wraps it with MUCREFILERR and sends it to the system log.
 * In addition, some messages require cleanup if they are emitted past a certain point in the processing (said point
 * setting the 'cleanup_needed' flag to TRUE.
 */
#define PUTMSG_MSG_ROUTER_CSA(CSAARG, VARCNT, ERRORID, ...)								\
MBSTART {														\
	mval		zpos;												\
															\
	if (IS_MUPIP_IMAGE)												\
		gtm_putmsg_csa(CSA_ARG(CSAARG) VARLSTCNT(VARCNT) ERRORID, __VA_ARGS__);					\
	else														\
	{														\
		/* Need to reflect the current error to the syslog - find entry ref to add to error. The VARLSTCNT	\
		 * computation is 8 for the prefix message, plus the VARLSTCNT() that would apply to the actual error	\
		 * message that got us here.										\
		 */													\
		getzposition(&zpos);											\
		send_msg_csa(CSA_ARG(CSAARG) VARLSTCNT((8 + VARCNT)) ERR_MUCREFILERR, 6, zpos.str.len, zpos.str.addr,	\
			     DB_LEN_STR(gv_cur_region), REG_LEN_STR(gv_cur_region), ERRORID, __VA_ARGS__);		\
	}														\
} MBEND
#define PUTMSG_ERROR_CSA(CSAARG, VARCNT, ERRORID, ...)									\
MBSTART {														\
	if (cleanup_needed)												\
		CLEANUP(EXIT_ERR);											\
	PUTMSG_MSG_ROUTER_CSA(CSAARG, VARCNT, ERRORID, __VA_ARGS__);							\
} MBEND
#define PUTMSG_WARN_CSA(CSAARG, VARCNT, ERRORID, ...) PUTMSG_MSG_ROUTER_CSA(CSAARG, VARCNT, ERRORID, __VA_ARGS__)

/* zOS is a currently unsupported platform so this macro remains unconverted but with an assertpro(FALSE) should
 * the zOS port ever be resurrected. In that case, uses of this macro need to be converted to PUTMSG_ERROR_CSA
 * invocations.
 */
#define SPRINTF_AND_PERROR_MVS(MESSAGE)					\
MBSTART {								\
	assertpro(FALSE);						\
	save_errno = errno;						\
	SPRINTF(errbuff, MESSAGE, path, realfiletag, TAG_BINARY);	\
	errno = save_errno;						\
	PERROR(errbuff);						\
} MBEND

GBLREF	gd_region		*gv_cur_region;
GBLREF	jnlpool_addrs_ptr_t	jnlpool;
GBLREF	sgmnt_addrs		*cs_addrs;
GBLREF	sgmnt_data_ptr_t	cs_data;
GBLREF	uint4			ydbDebugLevel;
#ifdef DEBUG
GBLREF	boolean_t		in_mu_cre_file;
#endif

STATICDEF	int	mu_cre_file_fd;		/* needed for "mu_cre_file_ch" */
STATICDEF	char	*mu_cre_file_path;	/* needed for "mu_cre_file_ch" */

error_def(ERR_DBBLKSIZEALIGN);
error_def(ERR_DBFILECREATED);
error_def(ERR_DBOPNERR);
error_def(ERR_DSKSPCCHK);
error_def(ERR_FILECREERR);
error_def(ERR_FNTRANSERROR);
error_def(ERR_LOWSPACECRE);
error_def(ERR_MUCREFILERR);
error_def(ERR_MUNOSTRMBKUP);
error_def(ERR_NOCREMMBIJ);
error_def(ERR_NOCRENETFILE);
error_def(ERR_NOSPACECRE);
error_def(ERR_PARNORMAL);
error_def(ERR_PREALLOCATEFAIL);
error_def(ERR_RAWDEVUNSUP);

/* Condition handler primarily to handle ERR_MEMORY errors by cleaning up the file that we created
 * before passing on control to higher level condition handlers.
 */
CONDITION_HANDLER(mu_cre_file_ch)
{
	START_CH(TRUE);
	if ((SUCCESS == SEVERITY) || (INFO == SEVERITY))
	{
		assert(FALSE);	/* don't know of any possible INFO/SUCCESS errors */
		CONTINUE;			/* Keep going for non-error issues */
	}
	CLEANUP(EXIT_ERR);
	NEXTCH;
}

unsigned char mu_cre_file(void)
{
	char		path[MAX_FBUFF + 1], errbuff[512];
	unsigned char	buff[DISK_BLOCK_SIZE];
	int		i, lower, upper, norm_vbn;
        ssize_t         status;
	uint4		raw_dev_size;		/* size of a raw device, in bytes */
	int4		save_errno;
	gtm_uint64_t	avail_blocks, blocks_for_create, blocks_for_extension, delta_blocks;
	file_control	fc;
	mstr		file;
	parse_blk	pblk;
	unix_db_info	udi_struct, *udi;
	char		*fgets_res;
	gd_segment	*seg;
	gd_region	*baseDBreg;
	char		hash[GTMCRYPT_HASH_LEN];
	int		gtmcrypt_errno, retcode, perms, user_id, group_id;
	off_t		new_eof;
	uint4		fsb_size;
	boolean_t	cleanup_needed;
	sgmnt_addrs	*baseDBcsa;
	struct stat	stat_buf;
	struct perm_diag_data	pdd;
	ZOS_ONLY(int	realfiletag;)
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	DEBUG_ONLY(mu_cre_file_fd = FD_INVALID);
	DEBUG_ONLY(mu_cre_file_path = NULL);
	cleanup_needed = FALSE;
	DEBUG_ONLY(in_mu_cre_file = TRUE;)
	assert((-(SIZEOF(uint4) * 2) & SIZEOF_FILE_HDR_DFLT) == SIZEOF_FILE_HDR_DFLT);
	cs_addrs = &udi_struct.s_addrs;
	cs_data = (sgmnt_data_ptr_t)NULL;	/* for CLEANUP */
	memset(&pblk, 0, SIZEOF(pblk));
	pblk.fop = (F_SYNTAXO | F_PARNODE);
	pblk.buffer = path;
	pblk.buff_size = MAX_FBUFF;
	file.addr = (char*)gv_cur_region->dyn.addr->fname;
	file.len = gv_cur_region->dyn.addr->fname_len;
	strncpy(path, file.addr, file.len);
	*(path + file.len) = '\0';
	if (is_raw_dev(path))
	{
		PUTMSG_ERROR_CSA(cs_addrs, 4, ERR_RAWDEVUNSUP, 2, REG_LEN_STR(gv_cur_region));
		return EXIT_ERR;
	}
	pblk.def1_buf = DEF_DBEXT;
	pblk.def1_size = SIZEOF(DEF_DBEXT) - 1;
	if (ERR_PARNORMAL != (retcode = parse_file(&file, &pblk)))	/* Note assignment */
	{
		PUTMSG_ERROR_CSA(cs_addrs, 4, ERR_FNTRANSERROR, 2, REG_LEN_STR(gv_cur_region));
		return EXIT_ERR;
	}
	path[pblk.b_esl] = 0;
	if (pblk.fnb & F_HAS_NODE)
	{	/* Remote node specification given */
		assert(pblk.b_node);
		PUTMSG_ERROR_CSA(cs_addrs, 4, ERR_NOCRENETFILE, 2, LEN_AND_STR(path));
		return EXIT_WRN;
	}
	udi = &udi_struct;
	memset(udi, 0, SIZEOF(unix_db_info));
	/* Check if this file is an encrypted database. If yes, do init */
	if (IS_ENCRYPTED(gv_cur_region->dyn.addr->is_encrypted))
	{
		assert(!TO_BE_ENCRYPTED(gv_cur_region->dyn.addr->is_encrypted));
		INIT_PROC_ENCRYPTION(gtmcrypt_errno);
		if (0 != gtmcrypt_errno)
		{
			GTMCRYPT_REPORT_ERROR(gtmcrypt_errno, gtm_putmsg, file.len, file.addr);
			return EXIT_ERR;
		}
	}
	mu_cre_file_fd = OPEN3(pblk.l_dir, O_CREAT | O_EXCL | O_RDWR, 0600);
	if (FD_INVALID == mu_cre_file_fd)
	{	/* Avoid error message if file already exists (another process created it) for AUTODBs that are NOT also
		 * STATSDBs.
		 */
		save_errno = errno;
		TREF(mu_cre_file_openrc) = errno;		/* Save for "gvcst_init" */
		/* If this is an AUTODB (but not a STATSDB) and the file already exists, this is not an error (some other
		 * process created the file. This is ok so return as if we created it.
		 */
		if (IS_AUTODB_REG(gv_cur_region) && !IS_STATSDB_REG(gv_cur_region) && (EEXIST == errno))
			return EXIT_NRM;
		/* Suppress EEXIST messages for statsDBs */
		if (!IS_STATSDB_REG(gv_cur_region) || (EEXIST != errno))
			PUTMSG_ERROR_CSA(cs_addrs, 5, ERR_DBOPNERR, 2, LEN_AND_STR(path), save_errno);
		return EXIT_ERR;
	}
	cleanup_needed = TRUE;			/* File open now so cleanup needed */
	mu_cre_file_path = &path[0];	/* needed by "mu_cre_file_ch" */
	/* mu_cre_file_fd is also needed by "mu_cre_file_ch" but that is already set */
	ESTABLISH_RET(mu_cre_file_ch, EXIT_ERR);
#	ifdef __MVS__
	if (-1 == gtm_zos_set_tag(mu_cre_file_fd, TAG_BINARY, TAG_NOTTEXT, TAG_FORCE, &realfiletag))
		SPRINTF_AND_PERROR_MVS("Error setting tag policy for file %s (%d) to %d\n");
#	endif
	if (0 != (save_errno = disk_block_available(mu_cre_file_fd, &avail_blocks, FALSE)))
	{
		errno = save_errno;
		PUTMSG_ERROR_CSA(cs_addrs, 5, ERR_DSKSPCCHK, 2, LEN_AND_STR(path), errno); /* Note: Internally invokes CLEANUP */
		REVERT;
		return EXIT_ERR;
	}
	seg = gv_cur_region->dyn.addr;
	seg->read_only = FALSE;
	if (seg->asyncio)
	{	/* AIO = ON, implies we need to use O_DIRECT. Check for db vs fs blksize alignment issues. */
		fsb_size = get_fs_block_size(mu_cre_file_fd);
		if (0 != (seg->blk_size % fsb_size))
		{
			PUTMSG_ERROR_CSA(cs_addrs, 6, ERR_DBBLKSIZEALIGN, 4, LEN_AND_STR(path), seg->blk_size, fsb_size);
				/* Note: Above macro internally invokes CLEANUP(EXIT_ERR) */
			REVERT;
			return EXIT_ERR;
		}
	}
	/* Blocks_for_create is in the unit of DISK_BLOCK_SIZE */
	blocks_for_create = (gtm_uint64_t)(DIVIDE_ROUND_UP(SIZEOF_FILE_HDR_DFLT, DISK_BLOCK_SIZE) + 1
					   + (seg->blk_size / DISK_BLOCK_SIZE
					      * (gtm_uint64_t)((DIVIDE_ROUND_UP(seg->allocation, BLKS_PER_LMAP - 1))
							       + seg->allocation)));
	blocks_for_extension = (seg->blk_size / DISK_BLOCK_SIZE
				* ((DIVIDE_ROUND_UP(EXTEND_WARNING_FACTOR * (gtm_uint64_t)seg->ext_blk_count,
						    BLKS_PER_LMAP - 1))
				   + EXTEND_WARNING_FACTOR * (gtm_uint64_t)seg->ext_blk_count));
	if (!(ydbDebugLevel & GDL_IgnoreAvailSpace))
	{	/* Bypass this space check if debug flag above is on. Allows us to create a large sparse DB
		 * in space it could never fit it if wasn't sparse. Needed for some tests.
		 * Also, if the anticipatory freeze scheme is in effect at this point, we would have issued
		 * a NOSPACECRE warning (see NOSPACEEXT message which goes through a similar transformation).
		 * But at this point, we are guaranteed to not have access to the journal pool or csa both
		 * of which are necessary for the INST_FREEZE_ON_ERROR_ENABLED(csa) macro so we don't bother
		 * to do the warning transformation in this case. The only exception to this is a statsdb
		 * which is anyways not journaled so need not worry about INST_FREEZE_ON_ERROR_ENABLED.
		 */
		assert(((NULL == jnlpool) || (NULL == jnlpool->jnlpool_ctl)) || IS_AUTODB_REG(gv_cur_region));
		if (avail_blocks < blocks_for_create)
		{
			PUTMSG_ERROR_CSA(cs_addrs, 6, ERR_NOSPACECRE, 4, LEN_AND_STR(path), &blocks_for_create, &avail_blocks);
				/* Note: Above macro internally invokes CLEANUP(EXIT_ERR) */
			if (IS_MUPIP_IMAGE)
				send_msg_csa(CSA_ARG(cs_addrs) VARLSTCNT(6) ERR_NOSPACECRE, 4, LEN_AND_STR(path),
					     &blocks_for_create, &avail_blocks);
			REVERT;
			return EXIT_ERR;
		}
		delta_blocks = avail_blocks - blocks_for_create;
		if (delta_blocks < blocks_for_extension)
		{
			PUTMSG_WARN_CSA(cs_addrs,8, ERR_LOWSPACECRE, 6, LEN_AND_STR(path), EXTEND_WARNING_FACTOR,
					&blocks_for_extension, DISK_BLOCK_SIZE, &delta_blocks);
			if (IS_MUPIP_IMAGE)	/* Is not mupip, msg already went to operator log */
				send_msg_csa(CSA_ARG(cs_addrs) VARLSTCNT(8) ERR_LOWSPACECRE, 6, LEN_AND_STR(path),
					     EXTEND_WARNING_FACTOR, &blocks_for_extension, DISK_BLOCK_SIZE, &delta_blocks);
		}
	}
	gv_cur_region->dyn.addr->file_cntl = &fc;
	memset(&fc, 0, SIZEOF(file_control));
	fc.file_info = (void*)&udi_struct;
	udi->fd = mu_cre_file_fd;
	cs_data = (sgmnt_data_ptr_t)malloc(SIZEOF_FILE_HDR_DFLT);
	memset(cs_data, 0, SIZEOF_FILE_HDR_DFLT);
	cs_data->createinprogress = TRUE;
	cs_data->semid = INVALID_SEMID;
	cs_data->shmid = INVALID_SHMID;
	/* We want our datablocks to start on what would be a block boundary within the file which will aid I/O
	 * so pad the fileheader if necessary to make this happen.
	 */
	norm_vbn = DIVIDE_ROUND_UP(SIZEOF_FILE_HDR_DFLT, DISK_BLOCK_SIZE) + 1;
	assert(START_VBN_CURRENT >= norm_vbn);
	cs_data->start_vbn = START_VBN_CURRENT;
	cs_data->free_space += (START_VBN_CURRENT - norm_vbn) * DISK_BLOCK_SIZE;
	cs_data->acc_meth = gv_cur_region->dyn.addr->acc_meth;
	if ((dba_mm == cs_data->acc_meth) && (gv_cur_region->jnl_before_image))
	{
		PUTMSG_ERROR_CSA(cs_addrs, 4, ERR_NOCREMMBIJ, 2, LEN_AND_STR(path));
			/* Note: Above macro internally invokes CLEANUP(EXIT_ERR) */
		REVERT;
		return EXIT_ERR;
	}
	cs_data->trans_hist.total_blks = gv_cur_region->dyn.addr->allocation;
	/* There are (bplmap - 1) non-bitmap blocks per bitmap, so add (bplmap - 2) to number of non-bitmap blocks
	 * and divide by (bplmap - 1) to get total number of bitmaps for expanded database. (must round up in this
	 * manner as every non-bitmap block must have an associated bitmap)
	 */
	cs_data->trans_hist.total_blks += DIVIDE_ROUND_UP(cs_data->trans_hist.total_blks, BLKS_PER_LMAP - 1);
	cs_data->extension_size = gv_cur_region->dyn.addr->ext_blk_count;
	/* Check if this file is an encrypted database. If yes, do init */
	if (IS_ENCRYPTED(gv_cur_region->dyn.addr->is_encrypted))
	{
		GTMCRYPT_HASH_GEN(cs_addrs, STRLEN(path), path, 0, NULL, hash, gtmcrypt_errno);
		if (0 != gtmcrypt_errno)
		{
			GTMCRYPT_REPORT_ERROR(gtmcrypt_errno, gtm_putmsg, file.len, file.addr);
			REVERT;
			CLEANUP(EXIT_ERR);
			return EXIT_ERR;
		}
		memcpy(cs_data->encryption_hash, hash, GTMCRYPT_HASH_LEN);
		SET_AS_ENCRYPTED(cs_data->is_encrypted); /* Mark this file as encrypted */
		INIT_DB_OR_JNL_ENCRYPTION(cs_addrs, cs_data, STRLEN(path), path, gtmcrypt_errno);
		if (0 != gtmcrypt_errno)
		{
			GTMCRYPT_REPORT_ERROR(gtmcrypt_errno, gtm_putmsg, file.len, file.addr);
			REVERT;
			CLEANUP(EXIT_ERR);
			return EXIT_ERR;
		}
	} else
		SET_AS_UNENCRYPTED(cs_data->is_encrypted);
	cs_data->non_null_iv = TRUE;
	cs_data->encryption_hash_cutoff = UNSTARTED;
	cs_data->encryption_hash2_start_tn = 0;
	cs_data->span_node_absent = TRUE;
	cs_data->maxkeysz_assured = TRUE;
	mucregini(cs_data->trans_hist.total_blks);
	cs_data->createinprogress = FALSE;
	ASSERT_NO_DIO_ALIGN_NEEDED(udi);	/* because we are creating the database and so effectively have standalone access */
	DB_LSEEKWRITE(cs_addrs, udi, udi->fn, udi->fd, 0, cs_data, SIZEOF_FILE_HDR_DFLT, status);
	if (0 != status)
	{
		PUTMSG_ERROR_CSA(cs_addrs, 7, ERR_FILECREERR, 4, LEN_AND_LIT("writing out file header"), LEN_AND_LIT(path), status);
			/* Note: Above macro internally invokes CLEANUP(EXIT_ERR) */
		REVERT;
		return EXIT_ERR;
	}
	new_eof = (off_t)BLK_ZERO_OFF(cs_data->start_vbn) + (off_t)cs_data->trans_hist.total_blks * cs_data->blk_size;
	status = db_write_eof_block(udi, udi->fd, cs_data->blk_size, new_eof, &(TREF(dio_buff)));
	if (0 != status)
	{
		PUTMSG_ERROR_CSA(cs_addrs, 7, ERR_FILECREERR, 4, LEN_AND_LIT("writing out end-of-file marker"), LEN_AND_LIT(path),
				 status); /* Note: Above macro internally invokes CLEANUP(EXIT_ERR) */
		REVERT;
		return EXIT_ERR;
	}
#	if !defined(__APPLE__)
	if (!cs_data->defer_allocate)
	{
		status = posix_fallocate(udi->fd, 0, BLK_ZERO_OFF(cs_data->start_vbn) +
					 ((off_t)(cs_data->trans_hist.total_blks + 1) * cs_data->blk_size));
		if (0 != status)
		{
			assert(ENOSPC == status);
			PUTMSG_ERROR_CSA(cs_addrs, 5, ERR_PREALLOCATEFAIL, 2, DB_LEN_STR(gv_cur_region), status);
				/* Note: Above macro internally invokes CLEANUP(EXIT_ERR) */
			REVERT;
			return EXIT_ERR;
		}
	}
#	endif
	/* If we are opening a statsDB, use IPC type permissions derived from the baseDB */
	if (IS_STATSDB_REG(gv_cur_region))
	{
		STATSDBREG_TO_BASEDBREG(gv_cur_region, baseDBreg);
		assert(baseDBreg->open);
		baseDBcsa = &FILE_INFO(baseDBreg)->s_addrs;
		STAT_FILE((char *)baseDBcsa->nl->fname, &stat_buf, retcode);
		if (0 > retcode)
		{	/* Should be rare-if-ever message as we just opened the baseDB so it should be there */
			save_errno = errno;
			PUTMSG_ERROR_CSA(cs_addrs, 7, ERR_FILECREERR, 4,
					 LEN_AND_LIT("getting base file information"), LEN_AND_STR(path), save_errno);
				/* Note: Above macro internally invokes CLEANUP(EXIT_ERR) */
			REVERT;
			return EXIT_ERR;
		}
		if (!gtm_permissions(&stat_buf, &user_id, &group_id, &perms, PERM_IPC, &pdd))
		{	/* Not sure what could cause this as we would have done the same call when opening the baseDB but
			 * make sure it is present just in case.
			 */
			PUTMSG_ERROR_CSA(cs_addrs, 7, ERR_FILECREERR, 4,
					 LEN_AND_LIT("obtaining permissions from base DB"),  LEN_AND_STR(path), EPERM);
				/* Note: Above macro internally invokes CLEANUP(EXIT_ERR) */
			REVERT;
			return EXIT_ERR;
		}
	} else
		perms = 0666;
	if (-1 == CHMOD(pblk.l_dir, perms))
	{
		save_errno = errno;
		PUTMSG_WARN_CSA(cs_addrs, 7, MAKE_MSG_WARNING(ERR_FILECREERR), 4, LEN_AND_LIT("changing file mode"),
				LEN_AND_LIT(path), save_errno);
		REVERT;
		CLEANUP(EXIT_WRN);
		return EXIT_WRN;
	}
	if ((32 * 1024 - SIZEOF(shmpool_blk_hdr)) < cs_data->blk_size)
		PUTMSG_WARN_CSA(cs_addrs, 5, ERR_MUNOSTRMBKUP, 3, RTS_ERROR_STRING(path), 32 * 1024 - DISK_BLOCK_SIZE);
	if (!(RDBF_AUTODB & gv_cur_region->reservedDBFlags))
		PUTMSG_WARN_CSA(cs_addrs, 4, ERR_DBFILECREATED, 2, LEN_AND_STR(path));
	REVERT;
	CLEANUP(EXIT_NRM);
	return EXIT_NRM;
}
