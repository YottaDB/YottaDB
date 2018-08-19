/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
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

#ifndef GDSFHEAD_H_INCLUDED
#define GDSFHEAD_H_INCLUDED

/* gdsfhead.h */
/* this requires gdsroot.h gtm_facility.h fileinfo.h gdsbt.h */

#include <sys/types.h>
#include "gdsdbver.h"
#include "gtm_unistd.h"
#include "gtm_limits.h"
#include "gtm_stdlib.h"
#include "gtm_string.h"
#include "send_msg.h"
#include "iosp.h"
#include "repl_instance.h"
#include "gtmcrypt.h" /* for gtmcrypt_key_t */
#include "gtm_libaio.h"
#include "gtm_reservedDB.h"

#define CACHE_STATE_OFF SIZEOF(que_ent)

error_def(ERR_ACTCOLLMISMTCH);
error_def(ERR_DBCRERR);
error_def(ERR_DBENDIAN);
error_def(ERR_DBFLCORRP);
error_def(ERR_DBROLLEDBACK);
error_def(ERR_ERRCALL);
error_def(ERR_GVIS);
error_def(ERR_GVSUBOFLOW);
error_def(ERR_MMFILETOOLARGE);
error_def(ERR_NCTCOLLSPGBL);
error_def(ERR_OFRZAUTOREL);
error_def(ERR_OFRZCRITREL);
error_def(ERR_OFRZCRITSTUCK);
error_def(ERR_REPLINSTACC);
error_def(ERR_REPLINSTMISMTCH);
error_def(ERR_REPLINSTNOSHM);
error_def(ERR_REPLREQROLLBACK);
error_def(ERR_SCNDDBNOUPD);
error_def(ERR_SRVLCKWT2LNG);
error_def(ERR_SSATTACHSHM);
error_def(ERR_SSFILOPERR);
error_def(ERR_STACKCRIT);
error_def(ERR_STACKOFLOW);
error_def(ERR_TEXT);
error_def(ERR_TNTOOLARGE);
error_def(ERR_TNWARN);
error_def(ERR_TPRETRY);
error_def(ERR_UNIMPLOP);

#define FULL_FILESYSTEM_WRITE 1
#define FULL_DATABASE_WRITE 2

#ifdef USE_LIBAIO
#	define	SYS_IOCB(CR)	CR->aiocb.sys_iocb
#else
#	define	SYS_IOCB(CR)	CR->aiocb
#endif

/* Cache record */
typedef struct cache_rec_struct
{
	struct
	{
		sm_off_t 	fl;
		sm_off_t	bl;
	}
			blkque,		/* cache records whose block numbers hash to the same location */
			state_que;	/* cache records in same state (either wip or active) */
	union
	{
		short	semaphore;
		volatile int4	latch;		/* int required for atomic swap on Unix */
			/* volatile required as this value is referenced outside of the lock in db_csh_getn() */
	} interlock;
	block_id	blk;
	uint4		refer;		/* reference bit for the clock algorithm */
	enum db_ver	ondsk_blkver;	/* Actual block version from block header as it exists on disk
					   (prior to any dynamic conversion that may have occurred when read in).
					*/
	/* Keep our 64 bit fields up front */
	/* this point should be quad-word aligned */
	trans_num	dirty;		/* block has been modified since last written to disk; used by bt_put, db_csh_getn
					 * mu_rndwn_file wcs_recover, secshr_db_clnup, wr_wrtfin_all and extensively by the ccp */
	trans_num	flushed_dirty_tn;	/* value of dirty at the time of flushing */
	trans_num	tn;
	sm_off_t	bt_index;	/* offset to corresponding bt_rec when this cache-record was last modified (bt->blk
					 *	and cr->blk would be identical). Maintained as a non-zero value even if cr->dirty
					 *	becomes non-zero later (due to a flush). Additionally, if this cache-record is a
					 *	twin, cr->bt_index helps distinguish the OLDER and NEWER twins. bt_index is
					 *	ZERO for the OLDER twin and NON-ZERO for the newer TWIN.
					 */
	sm_off_t	buffaddr;	/* offset to buffer holding actual data */
	sm_off_t	twin;		/* If non-zero, this points to another cache-record which contains the same block but
					 * a different state of the block. A twin for a block is created if a
					 * process holding crit and attempting to commit updates to a block finds that block
					 * being concurrently written to disk. If asyncio=OFF, this field is mostly zero.
					 * In addition to the above, this field is also non-zero in an exception case where
					 * cr->stopped is TRUE in which case the "twin" points to the cache_rec holding the
					 * before-image for wcs_recover to backup.
					 */
	off_jnl_t	jnl_addr;	/* offset from bg_update to prevent wcs_wtstart from writing a block ahead of the journal */
	boolean_t	stopped;	/* TRUE indicates to wcs_recover that secshr_db_clnup built the block */
	global_latch_t	rip_latch;	/* for read_in_progress - note contains extra 16 bytes for HPPA. Usage note: this
					   latch is used on those platforms where read_in_progress is not directly updated
					   by atomic routines/instructions. As such there needs be no cache line padding between
					   this field and read_in_progress.
					 */
	uint4		data_invalid;	/* non-zero pid from bg_update indicates we are in the middle of a "gvcst_blk_build"
					 * and so the block contents are not clean. secshr_db_clnup/wcs_recover check this
					 * field and take appropriate action to recover this cr.
					 */
	int4		epid;		/* set by wcs_wtstart to id the write initiator; cleared by wcs_wtstart/wcs_wtfini
					 * used by t_commit_cleanup, secshr_db_clnup and wcs_recover */
	int4		cycle;		/* relative stamp indicates changing versions of the block for concurrency checking */
	int4		r_epid;		/* set by db_csh_getn, cleared by t_qread, bg_update, wcs_recover or secshr_db_clnup
					 * used to check for process leaving without releasing the buffer
					 * must be word aligned on the VAX */
	struct aiocb	aiocb;		/* Used for asynchronous I/O if BG access method and csd->asyncio is TRUE */
	CNTR4DCL(read_in_progress, 10);	/* -1 for normal and 0 for rip used by t_qread and checked by others */
	uint4		in_tend;	/* non-zero pid from bg_update indicates secshr_db_clnup should finish update */
	uint4		in_cw_set;	/* non-zero pid from t_end, tp_tend or bg_update protects block from db_csh_getn;
					 * returned to 0 by t_end, tp_tend or t_commit_cleanup */
	bool		wip_is_encr_buf;/* TRUE if cache-record is WIP (write-in-progress) and its encrypted global buffer
					 * (not unencrypted global buffer) was used to issue the write in "wcs_wtstart".
					 * Used by "wcs_wtfini" to reissue the write. This field is not maintained
					 * when the cr is not in WIP status i.e. this is used only by the
					 * DB_LSEEKWRITEASYNCSTART and DB_LSEEKWRITEASYNCRESTART macros.
					 */
	bool		backup_cr_is_twin;	/* TRUE if cr corresponding to the before-image (used by BG_BACKUP_BLOCK)
						 * is the twin of this cache-record.
						 */
	bool		aio_issued;	/* set to TRUE after the asyncio has been issued in wcs_wtstart.
					 * set to FALSE before cr->epid is set to a non-zero value in wcs_wtstart.
					 * ONLY if epid is non-zero AND aio_issued is TRUE, can the write be considered
					 * in progress. This is used by wcs_recover to decide whether to place a cr into
					 * the active or wip queue.
					 */
	bool		needs_first_write; 	/* If this block needs to be written to disk for the first time,
						 *  note it (only applicable for ydb_fullblockwrites) */
	/* bool		filler4bytealign[1];	 Note: Make sure any changes here are reflected in "cache_state_rec" too */
} cache_rec;

/* A note about cache line separation of the latches contained in these blocks. Because this block is duplicated
   many (potentially tens+ of) thousands of times in a running system, we have decided against providing cacheline
   padding so as to force each cache record into a separate cacheline (due to it containing a latch and/or atomic
   counter field) to prevent processes from causing interference with each other. We decided that the probability
   of two processes working on adjacent cache records simultaneously was low enough that the interference was
   minimal whereas increasing the cache record size to prevent that interference could cause storage problems
   on some platforms where processes are already running near the edge.
*/

/* cache_state record */
typedef struct
{
	struct
	{
		sm_off_t 	fl;
		sm_off_t	bl;
	}
			state_que;	/* WARNING from this point, this structure must be identical to a cache_rec */
	union
	{
		short	semaphore;
		volatile int4	latch;		/* int required for atomic swap on Unix */
			/* volatile required as this value is referenced outside of the lock in db_csh_getn() */
	} interlock;
	block_id	blk;
	uint4		refer; 		/* reference bit for the LRU algorithm */
	enum db_ver	ondsk_blkver;	/* Actual block version from block header as it exists on disk
					   (prior to any dynamic conversion that may have occurred when read in).
					*/
	/* Keep our 64 bit fields up front */
	/* this point should be quad-word aligned */
	trans_num	dirty;		/* block has been modified since last written to disk; used by bt_put, db_csh_getn
					 * mu_rndwn_file wcs_recover, secshr_db_clnup, wr_wrtfin_all and extensively by the ccp */
	trans_num	flushed_dirty_tn;	/* value of dirty at the time of flushing */
	trans_num	tn;
	sm_off_t	bt_index;	/* offset to corresponding bt_rec when this cache-record was last modified (bt->blk
					 *	and cr->blk would be identical). Maintained as a non-zero value even if cr->dirty
					 *	becomes non-zero later (due to a flush). Additionally, if this cache-record is a
					 *	twin, cr->bt_index helps distinguish the OLDER and NEWER twins. bt_index is
					 *	ZERO for the OLDER twin and NON-ZERO for the newer TWIN.
					 */
	sm_off_t	buffaddr;	/* offset to buffer holding actual data*/
	sm_off_t	twin;		/* If non-zero, this points to another cache-record which contains the same block but
					 * a different state of the block. A twin for a block is created if a
					 * process holding crit and attempting to commit updates to a block finds that block
					 * being concurrently written to disk. If asyncio=OFF, this field is mostly zero.
					 * In addition to the above, this field is also non-zero in an exception case where
					 * cr->stopped is TRUE in which case the "twin" points to the cache_rec holding the
					 * before-image for wcs_recover to backup.
					 */
	off_jnl_t	jnl_addr;	/* offset from bg_update to prevent wcs_wtstart from writing a block ahead of the journal */
	boolean_t	stopped;	/* TRUE indicates to wcs_recover that secshr_db_clnup built the block */
	global_latch_t	rip_latch;	/* for read_in_progress - note contains extra 16 bytes for HPPA. Usage note: this
					   latch is used on those platforms where read_in_progress is not directly updated
					   by atomic routines/instructions. As such there needs be no cache line padding between
					   this field and read_in_progress.
					 */
	uint4		data_invalid;	/* non-zero pid from bg_update indicates t_commit_cleanup/wcs_recover should invalidate */
	int4		epid;		/* set by wcs_start to id the write initiator; cleared by wcs_wtstart/wcs_wtfini
					 * used by t_commit_cleanup, secshr_db_clnup and wcs_recover */
	int4		cycle;		/* relative stamp indicates changing versions of the block for concurrency checking */
	int4		r_epid;		/* set by db_csh_getn, cleared by t_qread, bg_update, wcs_recover or secshr_db_clnup
					 * used to check for process leaving without releasing the buffer
					 * must be word aligned on the VAX */
	struct aiocb	aiocb;		/* Used for asynchronous I/O if BG access method and csd->asyncio is TRUE */
	CNTR4DCL(read_in_progress, 10);	/* -1 for normal and 0 for rip used by t_qread and checked by others */
	uint4		in_tend;	/* non-zero pid from bg_update indicates secshr_db_clnup should finish update */
	uint4		in_cw_set;	/* non-zero pid from t_end, tp_tend or bg_update protects block from db_csh_getn;
					 * returned to 0 by t_end, tp_tend or t_commit_cleanup */
	bool		wip_is_encr_buf;/* TRUE if cache-record is WIP (write-in-progress) and its encrypted global buffer
					 * (not unencrypted global buffer) was used to issue the write in "wcs_wtstart".
					 * Used by "wcs_wtfini" to reissue the write. This field is not maintained
					 * when the cr is not in WIP status i.e. this is used only by the
					 * DB_LSEEKWRITEASYNCSTART and DB_LSEEKWRITEASYNCRESTART macros.
					 */
	bool		backup_cr_is_twin;	/* TRUE if cr corresponding to the before-image (used by BG_BACKUP_BLOCK)
						 * is the twin of this cache-record.
						 */
	bool		aio_issued;	/* set to TRUE after the asyncio has been issued in wcs_wtstart.
					 * set to FALSE before cr->epid is set to a non-zero value in wcs_wtstart.
					 * ONLY if epid is non-zero AND aio_issued is TRUE, can the write be considered
					 * in progress. This is used by wcs_recover to decide whether to place a cr into
					 * the active or wip queue.
					 */
	bool		needs_first_write; 	/* If this block needs to be written to disk for the first time,
						 *  note it (only applicable for ydb_fullblockwrites) */
	/*bool		filler4bytealign[1];	 Note: Make sure any changes here are reflected in "cache_state_rec" too */
} cache_state_rec;

#define		CR_BLKEMPTY		-1
#define		MBR_BLKEMPTY		-1
#define		FROZEN_BY_ROOT		(uint4)(0xFFFFFFFF)
#define		BACKUP_NOT_IN_PROGRESS	0x7FFFFFFF
#define		DB_CSH_RDPOOL_SZ	0x20	/* These many non-dirty buffers exist at all points in time in shared memory */

typedef struct
{
	cache_que_head	cacheq_wip,	/* write-in-progress queue */
			cacheq_active;	/* active queue */
	cache_rec	cache_array[1];	/*the first cache record*/
} cache_que_heads;

/* Define pointer types to some previously defined structures */

#ifdef DB64
# ifdef __osf__
#  pragma pointer_size(save)
#  pragma pointer_size(long)
# else
#  error UNSUPPORTED PLATFORM
# endif
#endif

typedef cache_que_head	*cache_que_head_ptr_t;
typedef cache_rec 	*cache_rec_ptr_t;
typedef cache_rec	**cache_rec_ptr_ptr_t;
typedef cache_state_rec	*cache_state_rec_ptr_t;
typedef cache_que_heads	*cache_que_heads_ptr_t;

gtm_uint64_t verify_queue_lock(que_head_ptr_t qhdr);
gtm_uint64_t verify_queue(que_head_ptr_t qhdr);

#ifdef DB64
# ifdef __osf__
#  pragma pointer_size(restore)
# endif
#endif

#ifdef DEBUG_QUEUE
#define VERIFY_QUEUE(base)		(void)verify_queue(base)
#define VERIFY_QUEUE_LOCK(base,latch)	(void)verify_queue_lock(base,latch)
#else
#define VERIFY_QUEUE(base)
#define VERIFY_QUEUE_LOCK(base,latch)
#endif

#define BLK_ZERO_OFF(start_vbn)			((start_vbn - 1) * DISK_BLOCK_SIZE)
#ifdef GTM64
#  define CHECK_LARGEFILE_MMAP(REG, MMAP_SZ)
#else
#  define CHECK_LARGEFILE_MMAP(REG, MMAP_SZ)										\
{															\
	assert(SIZEOF(gtm_uint64_t) == SIZEOF(MMAP_SZ));								\
	assert(0 < MMAP_SZ);												\
	if (MAXUINT4 < (gtm_uint64_t)(MMAP_SZ))										\
		rts_error_csa(CSA_ARG(REG2CSA(REG)) VARLSTCNT(6) ERR_MMFILETOOLARGE, 4, REG_LEN_STR(REG),		\
				DB_LEN_STR(REG));									\
}
# endif
# define MMAP_FD(FD, SIZE, OFFSET, READ_ONLY)	mmap((caddr_t)NULL, SIZE, MM_PROT_FLAGS(READ_ONLY), GTM_MM_FLAGS, FD, OFFSET)
# define MSYNC(BEGPTR, ENDPTR)			(BEGPTR ? DBG_ASSERT(BEGPTR < ENDPTR) msync(BEGPTR, (ENDPTR - BEGPTR), MS_SYNC)	\
							: 0)
# define MM_PROT_FLAGS(READ_ONLY)		(READ_ONLY ? PROT_READ : (PROT_READ | PROT_WRITE))
# define MM_BASE_ADDR(CSA) 			(sm_uc_ptr_t)CSA->db_addrs[0]
# define SET_MM_BASE_ADDR(CSA, CSD)
# ifdef _AIX
#  define MEM_MAP_SYSCALL "shmat()"
#  define MEM_UNMAP_SYSCALL "shmdt()"
# else
#  define MEM_MAP_SYSCALL "mmap()"
#  define MEM_UNMAP_SYSCALL "munmap()"
# endif

#define	GVKEY_INIT(GVKEY, KEYSIZE)							\
MBSTART {										\
	GBLREF gv_key	*gv_altkey;							\
	GBLREF gv_key	*gv_currkey;							\
	gv_key		*new_KEY, *old_KEY;						\
	int4		keySZ;								\
	DEBUG_ONLY(DCL_THREADGBL_ACCESS);						\
											\
	DEBUG_ONLY(SETUP_THREADGBL_ACCESS);						\
	old_KEY = GVKEY;								\
	keySZ = KEYSIZE;								\
	/* KEYSIZE should have been the output of a DBKEYSIZE command so		\
	 * should be a multiple of 4. Assert that.					\
	 */										\
	assert(ROUND_UP2(keySZ, 4) == keySZ);						\
	new_KEY = (gv_key *)malloc(SIZEOF(gv_key) - 1 + keySZ);				\
	assert((DBKEYSIZE(MAX_KEY_SZ) == KEYSIZE)					\
		|| ((GVKEY != gv_currkey) && (GVKEY != gv_altkey)));			\
	if ((NULL != old_KEY) && (PREV_KEY_NOT_COMPUTED != old_KEY->end))		\
	{										\
		/* Don't call GVKEY_INIT twice for same key. The only exception		\
		 * is if we are called from COPY_PREV_KEY_TO_GVT_CLUE in a		\
		 * restartable situation but TREF(donot_commit) should have		\
		 * been set to a special value then so check that.			\
		 */									\
		assert(TREF(donot_commit) | DONOTCOMMIT_COPY_PREV_KEY_TO_GVT_CLUE);	\
		assert(KEYSIZE >= old_KEY->top);					\
		assert(old_KEY->top > old_KEY->end);					\
		memcpy(new_KEY, old_KEY, SIZEOF(gv_key) - 1 + old_KEY->end);		\
		free(old_KEY);								\
	} else										\
	{										\
		new_KEY->base[0] = '\0';						\
		new_KEY->end = 0;							\
		new_KEY->prev = 0;							\
	}										\
	new_KEY->top = keySZ;								\
	GVKEY = new_KEY;								\
} MBEND

#define	GVKEY_FREE_IF_NEEDED(GVKEY)	\
MBSTART {				\
	if (NULL != GVKEY)		\
	{				\
		free(GVKEY);		\
		GVKEY = NULL;		\
	}				\
} MBEND

#define	GVKEYSIZE_INIT_IF_NEEDED									\
MBSTART {												\
	int		keySIZE;									\
													\
	GBLREF int4	gv_keysize;									\
	GBLREF gv_key	*gv_altkey;									\
	GBLREF gv_key	*gv_currkey;									\
													\
	if (!gv_keysize)										\
	{												\
		keySIZE = DBKEYSIZE(MAX_KEY_SZ);							\
		/* Have space to store at least MAX_MIDENT_LEN bytes as otherwise name-level $order	\
		 * (see op_gvorder/op_zprevious) could have buffer overflow issues in gv_currkey->base.	\
		 * Do ROUND_UP2(x,4) to keep an assert in GVKEY_INIT macro happy.			\
		 */											\
		assert((MAX_MIDENT_LEN + 3) < keySIZE);							\
		assert(keySIZE);									\
		gv_keysize = keySIZE;									\
		GVKEY_INIT(gv_currkey, keySIZE);							\
		GVKEY_INIT(gv_altkey, keySIZE);								\
	} else												\
	{												\
		assert((NULL != gv_currkey) && (NULL != gv_altkey) && gv_keysize			\
			&& (DBKEYSIZE(MAX_KEY_SZ) == gv_keysize)					\
			&& (gv_keysize == gv_currkey->top) && (gv_keysize == gv_altkey->top));		\
	}												\
} MBEND

/* Transform KEY to look at the immediately next key at the same subscript level as input KEY (like $order(KEY)).
 * For example if input KEY is ^x(1,2), come up with a key ^x(1,2++).
 */
#define	GVKEY_INCREMENT_ORDER(KEY)			\
MBSTART {						\
	int		end;				\
	unsigned char 	*base = KEY->base;		\
							\
	end = KEY->end;					\
	assert(KEY_DELIMITER == base[end - 1]);		\
	assert(KEY_DELIMITER == base[end]);		\
	assert(end + 1 < KEY->top);			\
	base[end - 1] = 1;				\
	base[end + 1] = KEY_DELIMITER;			\
	KEY->end = end + 1;				\
} MBEND

/* Undo work done by GVKEY_INCREMENT_ORDER */
#define	GVKEY_UNDO_INCREMENT_ORDER(KEY)			\
MBSTART {						\
	int	end;					\
							\
	assert(1 < KEY->end);				\
	end = KEY->end - 1;				\
	assert(1 == KEY->base[end - 1]);		\
	assert(KEY_DELIMITER == KEY->base[end]);	\
	assert(KEY_DELIMITER == KEY->base[end + 1]);	\
	assert(end + 1 < KEY->top);			\
	KEY->base[end - 1] = KEY_DELIMITER;		\
	KEY->base[end + 0] = KEY_DELIMITER;		\
	KEY->end = end;					\
} MBEND

/* Transform KEY to look at the immediately previous key at the same subscript level as input KEY (like $order(KEY)).
 * For example if input KEY is ^x(1,2), come up with a key ^x(1,2--).
 */
#define	GVKEY_DECREMENT_ORDER(KEY)			\
MBSTART {						\
	int	end;					\
							\
	end = KEY->end;					\
	assert(1 < end);				\
	assert(KEY_DELIMITER == KEY->base[end - 1]);	\
	assert(KEY_DELIMITER == KEY->base[end]);	\
	assert(0xFF != KEY->base[end - 2]);		\
	assert((end + 1) < KEY->top);			\
	KEY->base[end - 2] -= 1;			\
	KEY->base[end - 1] = 0xFF;			\
	KEY->base[end + 1] = KEY_DELIMITER;		\
	KEY->end = end + 1;				\
} MBEND

/* Undo work done by GVKEY_DECREMENT_ORDER */
#define	GVKEY_UNDO_DECREMENT_ORDER(KEY)			\
MBSTART {						\
	int	end;					\
							\
	assert(2 < KEY->end);				\
	end = KEY->end - 1;				\
	assert(0xFF == KEY->base[end - 1]);		\
	assert(KEY_DELIMITER != KEY->base[end - 2]);	\
	assert(KEY_DELIMITER == KEY->base[end]);	\
	assert(KEY_DELIMITER == KEY->base[end + 1]);	\
	assert((end + 1) < KEY->top);			\
	KEY->base[end - 2] += 1;			\
	KEY->base[end - 1] = KEY_DELIMITER;		\
	KEY->end = end;					\
} MBEND

/* Transform KEY to look at the immediately next KEY at any subscript level (like $query(KEY)).
 * For example if input KEY is ^x(1,2), come up with a key ^x(1,2,1) assuming that is the next node.
 */
#define	GVKEY_INCREMENT_QUERY(KEY)			\
MBSTART {						\
	int	end;					\
							\
	end = KEY->end;					\
	assert(KEY_DELIMITER == KEY->base[end - 1]);	\
	assert(KEY_DELIMITER == KEY->base[end]);	\
	assert(end + 2 < KEY->top);			\
	KEY->base[end] = 1;				\
	KEY->base[end + 1] = KEY_DELIMITER;		\
	KEY->base[end + 2] = KEY_DELIMITER;		\
	KEY->end += 2;					\
} MBEND

/* Transform KEY to look at the immediately previous key at the PREVIOUS subscript level as input KEY.
 * For example if input KEY is ^x(1,2), come up with a key ^x(1++).
 */
#define	GVKEY_INCREMENT_PREVSUBS_ORDER(KEY)			\
MBSTART {							\
	assert(KEY->prev);					\
	assert(KEY->end > KEY->prev);				\
	assert(KEY_DELIMITER == KEY->base[KEY->prev - 1]);	\
	assert(KEY_DELIMITER != KEY->base[KEY->prev]);		\
	assert(KEY_DELIMITER == KEY->base[KEY->end - 1]);	\
	assert(KEY_DELIMITER == KEY->base[KEY->end]);		\
	assert(KEY->end + 1 <= KEY->top);			\
	KEY->base[KEY->prev - 1] = 1;				\
} MBEND

/* Undo work done by GVKEY_INCREMENT_PREVSUBS_ORDER */
#define	GVKEY_UNDO_INCREMENT_PREVSUBS_ORDER(KEY)		\
MBSTART {							\
	assert(KEY->prev);					\
	assert(KEY->end > KEY->prev);				\
	assert(1 == KEY->base[KEY->prev - 1]);			\
	assert(KEY_DELIMITER != KEY->base[KEY->prev]);		\
	assert(KEY_DELIMITER == KEY->base[KEY->end - 1]);	\
	assert(KEY_DELIMITER == KEY->base[KEY->end]);		\
	assert(KEY->end + 1 <= KEY->top);			\
	KEY->base[KEY->prev - 1] = KEY_DELIMITER;		\
} MBEND

/* Transform KEY to look at the "" subscript at same subscript level as input KEY.
 * For example if input KEY is ^x(1,2), come up with a key ^x(1,"").
 */
#define	GVKEY_SET_SUBS_ZPREVIOUS(KEY, SAVECH)			\
MBSTART {							\
	assert(KEY->prev);					\
	assert(KEY->end > KEY->prev);				\
	assert(KEY_DELIMITER == KEY->base[KEY->prev - 1]);	\
	assert(KEY_DELIMITER != KEY->base[KEY->prev]);		\
	assert(KEY_DELIMITER == KEY->base[KEY->end - 1]);	\
	assert(KEY_DELIMITER == KEY->base[KEY->end]);		\
	assert(KEY->end + 1 <= KEY->top);			\
	SAVECH = KEY->base[KEY->prev];				\
	KEY->base[KEY->prev] = 1;				\
} MBEND

/* Undo work done by GVKEY_SET_SUBS_ZPREVIOUS */
#define	GVKEY_UNDO_SET_SUBS_ZPREVIOUS(KEY, SAVECH)		\
MBSTART {							\
	assert(KEY->prev);					\
	assert(KEY->end > KEY->prev);				\
	assert(1 == KEY->base[KEY->prev]);			\
	assert(KEY_DELIMITER == KEY->base[KEY->prev - 1]);	\
	assert(KEY_DELIMITER == KEY->base[KEY->end - 1]);	\
	assert(KEY_DELIMITER == KEY->base[KEY->end]);		\
	assert(KEY->end + 1 <= KEY->top);			\
	KEY->base[KEY->prev] = SAVECH;				\
} MBEND

/* This macro is used whenever we have found a MAP where a key KEYBASE maps to (using "gv_srch_map*" functions).
 * If the MAP entry one before EXACTLY matches the KEY, then some callers might want to see MAP-1 instead of MAP.
 * It is upto the caller to decide which one they want. By default they get MAP from gv_srch_map and can invoke
 * this macro to get MAP-1 in that special case.
 */
#define	BACK_OFF_ONE_MAP_ENTRY_IF_EDGECASE(KEYBASE, KEYLEN, MAP)				\
MBSTART {											\
	if (!memcmp(KEYBASE, ((MAP) - 1)->gvkey.addr, KEYLEN))					\
	{	/* KEYBASE starts at "MAP" which means, all keys of interest (just one before	\
		 * the incremented key) can never map to "MAP" so back off one map entry.	\
		 */										\
		(MAP)--;									\
		OPEN_BASEREG_IF_STATSREG(MAP);							\
	}											\
} MBEND

/* The below macro is invoked whenever we are about to return a map entry that corresponds to a statsdb region.
 * This means the caller is about to use this map entry to access a statsdb region. But it is possible this statsdb
 * region does not map to a real database file (e.g. if the corresponding baseDBreg has NOSTATS defined in the .gld
 * OR if the basedb has NOSTATS defined in its file header). In all such cases, we do not want the caller to error out
 * so we repoint the statsdb map entry to point to a basedb and since we don't expect to see ^%YGS nodes in the basedb
 * the access will return without any error (e.g. $GET will return "" etc. but no error).
 */
#define	OPEN_BASEREG_IF_STATSREG(MAP)										\
{														\
	gd_region	*baseDBreg, *statsDBreg;								\
														\
	statsDBreg = MAP->reg.addr;										\
	if (IS_STATSDB_REGNAME(statsDBreg))									\
	{													\
		STATSDBREG_TO_BASEDBREG(statsDBreg, baseDBreg);							\
		if (!baseDBreg->open)										\
			gv_init_reg(baseDBreg);									\
		if (!statsDBreg->open)										\
		{	/* statsDB did not get opened as part of baseDB open above. Possible if ydb_statshare	\
			 * is not set to 1. But user could still do a ZWR ^%YGS which would try to open		\
			 * statsDB in caller (who is not equipped to handle errors) so do the open of the	\
			 * statsDB now and silently handle errors like is done in "gvcst_init".	Any errors	\
			 * will cause the baseDB to have NOSTATS set which would make the map entry point to	\
			 * the baseDB thereby avoiding any user-visible errors even if they do ZWR ^%YGS.	\
			 * Indicate we want to do just "gvcst_init" of the statsDB, not the ^%YGS addition	\
			 * by passing DO_STATSDB_INIT_FALSE.							\
			 */											\
			gvcst_init_statsDB(baseDBreg, DO_STATSDB_INIT_FALSE);					\
		}												\
		/* If baseDB has NOSTATS defined in its file header, repoint map entry to non-statsDB region.	\
		 * This will prevent FILENOTFOUND errors on the statsdb file (.gst file) on access to ^%YGS	\
		 * nodes that map to non-existent statsdb regions.						\
		 */												\
		if (RDBF_NOSTATS & baseDBreg->reservedDBFlags)							\
			MAP->reg.addr = baseDBreg;								\
	}													\
}

/* The below macro is modeled pretty much like OPEN_BASEREG_IF_STATSREG except that this asserts that the
 * other macro is not needed. And that whatever that macro sets up is already set up that way.
 */
#ifdef DEBUG
#define	ASSERT_BASEREG_OPEN_IF_STATSREG(MAP)				\
{									\
	gd_region	*baseDBreg, *statsDBreg;			\
									\
	statsDBreg = MAP->reg.addr;					\
	if (IS_STATSDB_REGNAME(statsDBreg))				\
	{								\
		STATSDBREG_TO_BASEDBREG(statsDBreg, baseDBreg);		\
		assert(baseDBreg->open);				\
		assert(statsDBreg->open);				\
	}								\
}
#else
#define	ASSERT_BASEREG_OPEN_IF_STATSREG(MAP)
#endif

/* Calculate the # of subscripts in "KEY" and stores that in "NSUBS" */
#define	GET_NSUBS_IN_GVKEY(PTR, LEN, NSUBS)							\
MBSTART {											\
	unsigned char	*ptr, *ptrtop;								\
	int		nSubs;									\
												\
	ptr = (unsigned char *)PTR;								\
	ptrtop  = ptr + LEN;									\
	assert(ptr < ptrtop);									\
	nSubs = 0;										\
	for ( ; ptr < ptrtop; ptr++)								\
		if (KEY_DELIMITER == *ptr)							\
			nSubs++;								\
	NSUBS = nSubs;										\
} MBEND

#define WAS_OPEN_TRUE		TRUE
#define WAS_OPEN_FALSE		FALSE

#define SKIP_ASSERT_TRUE	TRUE
#define SKIP_ASSERT_FALSE	FALSE

/* Below macro sets open, opening and was_open fields of a given region after the corresponding
 * database for that region is opened. Also, if the region was not already open, the macro
 * invokes GVKEYSIZE_INIT_IF_NEEDED to allocate gv_currkey/gv_altkey if not already done.
 */
#define SET_REGION_OPEN_TRUE(REG, WAS_OPEN)									\
MBSTART {													\
	DEBUG_ONLY(GBLREF int4	gv_keysize;)									\
														\
	assert(!REG->was_open);											\
	assert(!REG->open);											\
	REG->open = TRUE;											\
	REG->opening = FALSE;											\
	if (WAS_OPEN)												\
	{													\
		REG->was_open = TRUE;										\
		TREF(was_open_reg_seen) = TRUE;									\
		assert(DBKEYSIZE(REG->max_key_size) <= gv_keysize);						\
	} else													\
		GVKEYSIZE_INIT_IF_NEEDED; /* sets up "gv_keysize", "gv_currkey" and "gv_altkey" in sync */	\
} MBEND

#define	REG_ACC_METH(REG)		(REG->dyn.addr->acc_meth)
#define	IS_REG_BG_OR_MM(REG)		IS_ACC_METH_BG_OR_MM(REG_ACC_METH(REG))
#define	IS_CSD_BG_OR_MM(CSD)		IS_ACC_METH_BG_OR_MM(CSD->acc_meth)
#define	IS_ACC_METH_BG_OR_MM(ACC_METH)	((dba_bg == ACC_METH) || (dba_mm == ACC_METH))

#define	SET_CSA_DIR_TREE(csa, keysize, reg)							\
MBSTART {											\
	if (NULL == csa->dir_tree)								\
	{											\
		assert(IS_REG_BG_OR_MM(reg));							\
		csa->dir_tree = targ_alloc(keysize, NULL, reg);					\
		GTMTRIG_ONLY(assert(NULL == csa->hasht_tree));					\
	} else											\
		assert((csa->dir_tree->gd_csa == csa) && (DIR_ROOT == csa->dir_tree->root));	\
} MBEND

#define	FREE_CSA_DIR_TREE(csa)							\
MBSTART {									\
	sgmnt_addrs	*lcl_csa;						\
	gv_namehead	*dir_tree, *hasht_tree;					\
										\
	lcl_csa = csa;								\
	GTMTRIG_ONLY(								\
		hasht_tree = lcl_csa->hasht_tree;				\
		if (NULL != hasht_tree)						\
		{								\
			assert(hasht_tree->gd_csa == csa);			\
			/* assert that TARG_FREE_IF_NEEDED will happen below */	\
			assert(1 == hasht_tree->regcnt);			\
			TARG_FREE_IF_NEEDED(hasht_tree);			\
			lcl_csa->hasht_tree = NULL;				\
		}								\
	)									\
	dir_tree = lcl_csa->dir_tree;						\
	assert(NULL != dir_tree);						\
	/* assert that TARG_FREE_IF_NEEDED will happen below */			\
	assert(1 == dir_tree->regcnt);						\
	TARG_FREE_IF_NEEDED(dir_tree);						\
	lcl_csa->dir_tree = NULL;						\
} MBEND

#define	ADD_TO_GVT_PENDING_LIST_IF_REG_NOT_OPEN(REG, GVT_PTR, GVT_PTR2)					\
MBSTART {												\
	gvt_container		*gvtc;									\
	DEBUG_ONLY(gv_namehead	*gvt;)									\
													\
	GBLREF	buddy_list	*gvt_pending_buddy_list;						\
	GBLREF	gvt_container	*gvt_pending_list;							\
													\
	/* For dba_cm or dba_user, don't add to pending list because those regions			\
	 * will never be opened by the client process (this process).					\
	 */												\
	if (!REG->open && IS_REG_BG_OR_MM(REG))								\
	{	/* Record list of all gv_targets that have been allocated BEFORE the			\
		 * region has been opened. Once the region gets opened, we will re-examine		\
		 * this list and reallocate them (if needed) since they have now been			\
		 * allocated using the region's max_key_size value which could potentially		\
		 * be different from the max_key_size value in the corresponding database		\
		 * file header.										\
		 */											\
		assert(NULL != gvt_pending_buddy_list);	/* should have been allocated by caller */	\
		DEBUG_ONLY(gvt = *GVT_PTR;)								\
		assert(NULL == is_gvt_in_pending_list(gvt));	/* do not add duplicates */		\
		gvtc = (gvt_container *)get_new_free_element(gvt_pending_buddy_list);			\
		gvtc->gvt_ptr = GVT_PTR;								\
		gvtc->gvt_ptr2 = GVT_PTR2;								\
		gvtc->gd_reg = REG;									\
		gvtc->next_gvtc = (struct gvt_container_struct *)gvt_pending_list;			\
		gvt_pending_list = gvtc;								\
	}												\
} MBEND

#define	PROCESS_GVT_PENDING_LIST(GREG, CSA)								\
MBSTART {												\
	GBLREF	gvt_container	*gvt_pending_list;							\
													\
	if (NULL != gvt_pending_list)									\
	{	/* Now that the region has been opened, check if there are any gv_targets that were	\
		 * allocated for this region BEFORE the open. If so, re-allocate them if necessary.	\
		 */											\
		process_gvt_pending_list(GREG, CSA);							\
	}												\
} MBEND

#define	TARG_FREE_IF_NEEDED(GVT)		\
MBSTART {					\
	GVT->regcnt--;				\
	if (!GVT->regcnt)			\
		targ_free(GVT);			\
} MBEND

#define		T_COMMIT_CRIT_PHASE0	1	/* csa->t_commit_crit gets set to this when reserving space for jnl record writing.
						 * The actual journal write happens after releasing crit in phase2 of commit for BG.
						 */
#define		T_COMMIT_CRIT_PHASE1	2	/* csa->t_commit_crit gets set to this during bg_update_phase1 for BG */
#define		T_COMMIT_CRIT_PHASE2	3	/* csa->t_commit_crit gets set to this during bg_update_phase2 for BG */

/* macro to check if we hold crit or are committing (with or without crit) */
#define		T_IN_CRIT_OR_COMMIT(CSA)	((CSA)->now_crit || (CSA)->t_commit_crit)

/* Macro to check if we hold crit or are committing (with or without crit) or are in wcs_wtstart for this region.
 * This is used in timer handling code to determine if it is ok to interrupt. We do not want to interrupt if holding
 * crit or in the midst of commit or in wcs_wtstart (in the last case, we could be causing another process HOLDING CRIT
 * on the region to wait in bg_update_phase1 if we hold the write interlock).
 */
#define		T_IN_CRIT_OR_COMMIT_OR_WRITE(CSA)	(T_IN_CRIT_OR_COMMIT(CSA) || (CSA)->in_wtstart)

/* Macro to check if we are committing (with or without crit) or are in wcs_wtstart for this region. This is used in timer
 * handling code to determine if it is ok to interrupt. We do not want to interrupt if we are in the midst of commit or
 * in wcs_wtstart (in the second case, we could be causing another process HOLDING CRIT on the region to wait in
 * bg_update_phase1 if we hold the write interlock).
 */
#define         T_IN_COMMIT_OR_WRITE(CSA)       ((CSA)->t_commit_crit || (CSA)->in_wtstart)

/* Macro to check if a database commit is past the point where it can be successfully rolled back.
 * If t_commit_crit is T_COMMIT_CRIT_PHASE2, "bg_update_phase2" could have happened on at least one block
 * and so we cannot roll back easily. If t_commit_crit is T_COMMIT_CRIT_PHASE1, "bg_update_phase1" could
 * have started on at least one block and so we cannot roll back easily. If it is T_COMMIT_CRIT_PHASE0,
 * only journal records have been written to the journal buffer and those should be easily rolled back.
 */
#define		T_UPDATE_UNDERWAY(CSA)	(T_COMMIT_CRIT_PHASE1 <= (CSA)->t_commit_crit)

/* Below macro sets csa->t_commit_crit to T_COMMIT_CRIT_PHASE1 to signal start of phase1 of db commit.
 * In addition, it also sets cnl->update_underway_tn which indicates to "mutex_salvage" that in case this process
 * gets kill -9ed between now and end of commit, and replication is turned on in this region, the
 * csa->jnl->jnl_buff->phase2_commit_array[] entry corresponding to this commit should be set up such that
 * when "jnl_phase2_salvage" is called later, it will write a JRT_NULL record on behalf of this transaction.
 * If a kill -9 happens between begin of commit and now, then a JRT_INCTN record will instead be written.
 * For non-journaled regions, cnl->update_underway_tn does not matter currently.
 */
#define	SET_T_COMMIT_CRIT_PHASE1(CSA, CNL, TN)							\
MBSTART {											\
	CSA->t_commit_crit = T_COMMIT_CRIT_PHASE1;	/* phase1 : lock database buffers */	\
	CNL->update_underway_tn = TN;								\
} MBEND

#define	SET_REG_SEQNO(CSA, SEQNO, SUPPLEMENTARY, STRM_INDEX, NEXT_STRM_SEQNO, SKIP_ASSERT)	\
{												\
	assert(SKIP_ASSERT || (CSA->hdr->reg_seqno < SEQNO));					\
	CSA->hdr->reg_seqno = SEQNO;								\
	if (SUPPLEMENTARY)									\
		CSA->hdr->strm_reg_seqno[STRM_INDEX] = NEXT_STRM_SEQNO;				\
}

/* the file header has relative pointers to its data structures so each process will malloc
 * one of these and fill it in with absolute pointers upon file initialization.
 */
#define GDS_REL2ABS(x)	(((sm_uc_ptr_t)cs_addrs->lock_addrs[0] + (sm_off_t)(x)))
#define GDS_ABS2REL(x) (sm_off_t)(((sm_uc_ptr_t)(x) - (sm_uc_ptr_t)cs_addrs->lock_addrs[0]))
#define GDS_ANY_REL2ABS(w,x) (((sm_uc_ptr_t)(w->lock_addrs[0]) + (sm_off_t)(x)))
#define GDS_ANY_ABS2REL(w,x) (sm_off_t)(((sm_uc_ptr_t)(x) - (sm_uc_ptr_t)w->lock_addrs[0]))
#define GDS_ANY_ENCRYPTGLOBUF(w,x) ((sm_uc_ptr_t)(w) + (sm_off_t)(x->nl->encrypt_glo_buff_off))
#define	ASSERT_IS_WITHIN_SHM_BOUNDS(ptr, csa)											\
	assert((NULL == (ptr)) || (((ptr) >= csa->db_addrs[0]) && ((0 == csa->db_addrs[1]) || ((ptr) < csa->db_addrs[1]))))

#ifdef DEBUG
#define DBG_ENSURE_PTR_IS_VALID_GLOBUFF(CSA, CSD, PTR)					\
MBSTART {										\
	cache_rec_ptr_t			cache_start;					\
	long				bufindx;					\
	sm_uc_ptr_t			bufstart;					\
											\
	cache_start = &(CSA)->acc_meth.bg.cache_state->cache_array[0];			\
	cache_start += CSD->bt_buckets;							\
	bufstart = (sm_uc_ptr_t)GDS_ANY_REL2ABS((CSA), cache_start->buffaddr);		\
	assert((PTR) >= bufstart);							\
	bufindx = (PTR - bufstart) / CSD->blk_size;					\
	assert(bufindx < CSD->n_bts);							\
	assert((bufstart + (bufindx * CSD->blk_size)) == (PTR));			\
} MBEND
#define DBG_ENSURE_PTR_IS_VALID_ENCTWINGLOBUFF(CSA, CSD, PTR)				\
MBSTART {										\
	cache_rec_ptr_t			cache_start;					\
	long				bufindx;					\
	sm_uc_ptr_t			bufstart;					\
											\
	cache_start = &(CSA)->acc_meth.bg.cache_state->cache_array[0];			\
	cache_start += CSD->bt_buckets;							\
	bufstart = (sm_uc_ptr_t)GDS_ANY_REL2ABS((CSA), cache_start->buffaddr);		\
	bufstart += (gtm_uint64_t)CSD->blk_size * CSD->n_bts;				\
	assert((PTR) >= bufstart);							\
	bufindx = (PTR - bufstart) / CSD->blk_size;					\
	assert(bufindx < CSD->n_bts);							\
	assert((bufstart + (bufindx * (gtm_uint64_t)CSD->blk_size)) == (PTR));		\
} MBEND

#define	DBG_ENSURE_OLD_BLOCK_IS_VALID(cse, is_mm, csa, csd)								\
MBSTART {														\
	cache_rec_ptr_t		cache_start;										\
	long			bufindx;										\
	sm_uc_ptr_t		bufstart;										\
	GBLREF	boolean_t	dse_running, write_after_image;								\
	GBLREF	uint4		process_id;										\
															\
	assert((gds_t_write != cse->mode) && (gds_t_write_recycled != cse->mode) && gds_t_writemap != cse->mode		\
		|| (NULL != cse->old_block));	/* don't miss writing a PBLK */						\
	if (NULL != cse->old_block)											\
	{														\
		if (!is_mm)												\
		{													\
			cache_start = &csa->acc_meth.bg.cache_state->cache_array[0];					\
			cache_start += csd->bt_buckets;									\
			bufstart = (sm_uc_ptr_t)GDS_ANY_REL2ABS(csa, cache_start->buffaddr);				\
			bufindx = (cse->old_block - bufstart) / csd->blk_size;						\
			assert(bufindx < csd->n_bts);									\
			assert(cse->blk == cache_start[bufindx].blk);							\
			assert(dse_running || write_after_image || (process_id == cache_start[bufindx].in_cw_set));	\
		} else													\
			assert(cse->old_block == MM_BASE_ADDR(csa) + (off_t)cse->blk * csd->blk_size);			\
	}														\
} MBEND

/* Check if a given address corresponds to a global buffer (BG) in database shared memory AND if
 * we are in phase2 of commit. If so check whether the corresponding cache-record is pinned.
 * Used by gvcst_blk_build to ensure the update array points to valid contents even though we don't hold crit.
 */
#define	DBG_BG_PHASE2_CHECK_CR_IS_PINNED(csa, seg)								\
MBSTART {													\
	cache_rec_ptr_t		cache_start;									\
	long			bufindx;									\
	sm_uc_ptr_t		bufstart, bufend, bufaddr;							\
														\
	GBLREF	uint4		process_id;									\
														\
	if ((seg)->len && (T_COMMIT_CRIT_PHASE2 == csa->t_commit_crit) && (dba_bg == csa->hdr->acc_meth))	\
	{													\
		cache_start = &csa->acc_meth.bg.cache_state->cache_array[0];					\
		cache_start += csa->hdr->bt_buckets;								\
		bufstart = (sm_uc_ptr_t)GDS_ANY_REL2ABS(csa, cache_start->buffaddr);				\
		bufend = bufstart + ((gtm_uint64_t)csa->hdr->n_bts * csa->hdr->blk_size);			\
		bufaddr = (sm_uc_ptr_t)(seg)->addr;								\
		/* Check if given address is within database shared memory range */				\
		if ((bufaddr >= bufstart) && (bufaddr < bufend))						\
		{												\
			bufindx = (bufaddr - bufstart) / csa->hdr->blk_size;					\
			assert(bufindx < csa->hdr->n_bts);							\
			/* Assert that we have the cache-record pinned */					\
			assert(process_id == cache_start[bufindx].in_cw_set);					\
		}												\
	}													\
} MBEND

/* Macro to check that we have not pinned any more buffers than we are updating.
 * This check is done only for BG access method and in dbg mode.
 * This is invoked by t_end/tp_tend just before beginning phase2 of commit.
 */
#define	DBG_CHECK_PINNED_CR_ARRAY_CONTENTS(csd, is_mm, crarray, crarrayindex)					\
MBSTART {													\
	GBLREF	boolean_t	write_after_image;								\
	GBLREF	uint4		process_id;									\
	cache_rec		*cr;										\
	int4			crindex, bplmap;								\
														\
	if (!is_mm)												\
	{													\
		bplmap = csd->bplmap;										\
		for (crindex = 0; crindex < crarrayindex; crindex++)						\
		{												\
			cr = crarray[crindex];									\
			if (process_id == cr->in_cw_set)							\
			{	/* We have pinned that cache-record implies we are planning on updating it	\
				 * (so should have set in_tend). There are 2 exceptions though.			\
				 *										\
				 * a) Since bitmap blocks are done with phase2 inside of crit, they should not	\
				 * show up in the pinned array list at end of phase1 for GT.M. But DSE is an	\
				 * exception as it could operate on a bitmap block as if it is updating a	\
				 * non-bitmap block (i.e. without invoking gvcst_map_build). MUPIP JOURNAL	\
				 * RECOVER also could do the same thing while applying an AIMG record. In both	\
				 * cases, "write_after_image" would be TRUE.					\
				 * 										\
				 * b) In case this is a twinned cache-record, for the older twin "in_cw_set"	\
				 * will be set to non-zero, but "in_tend" will be set to FALSE. Since we are	\
				 * outside of crit at this point, it is possible cr->twin field might be 0	\
				 * (could have gotten cleared by wcs_wtfini concurrently) so we cannot assert	\
				 * on the twin field but "cr->bt_index" should still be 0 since we have not	\
				 * yet finished the update on the newer twin so use that instead. Also twinning	\
				 * is enabled only if csd->asyncio is TRUE so check that as well.		\
				 */										\
				assert((process_id == cr->in_tend)						\
					|| ((0 == (cr->blk % bplmap)) && write_after_image)			\
					|| (csd->asyncio && !cr->bt_index));					\
			}											\
		}												\
	}													\
} MBEND

#else
#define DBG_ENSURE_PTR_IS_VALID_GLOBUFF(CSA, CSD, PTR)
#define DBG_ENSURE_PTR_IS_VALID_ENCTWINGLOBUFF(CSA, CSD, PTR)
#define DBG_ENSURE_OLD_BLOCK_IS_VALID(cse, is_mm, csa, csd)
#define DBG_BG_PHASE2_CHECK_CR_IS_PINNED(csa, bufaddr)
#define	DBG_CHECK_PINNED_CR_ARRAY_CONTENTS(csd, is_mm, crarray, crarrayindex)
#endif

/* The TP_CHANGE_REG macro is a replica of the tp_change_reg() routine to be used for performance considerations.
 * The TP_CHANGE_REG_IF_NEEDED macro tries to optimize on processing if reg is same as gv_cur_region. But it can be
 *	used only if the region passed is not NULL and if gv_cur_region, cs_addrs and cs_data are known to be in sync.
 * Note that timers can interrupt the syncing and hence any routines that are called by timers should be safe
 *	and use the TP_CHANGE_REG macro only.
 */
#define	TP_CHANGE_REG(reg)									\
MBSTART {											\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;						\
	gv_cur_region = reg;									\
	if (NULL == gv_cur_region || FALSE == gv_cur_region->open)				\
	{											\
		cs_addrs = (sgmnt_addrs *)0;							\
		cs_data = (sgmnt_data_ptr_t)0;							\
	} else											\
	{											\
		switch (REG_ACC_METH(reg))							\
		{										\
			case dba_mm:								\
			case dba_bg:								\
				cs_addrs = &FILE_INFO(gv_cur_region)->s_addrs;			\
				cs_data = cs_addrs->hdr;					\
				if (cs_addrs->jnlpool && (jnlpool != cs_addrs->jnlpool))	\
					jnlpool = cs_addrs->jnlpool;				\
				break;								\
			case dba_usr:								\
			case dba_cm:								\
				cs_addrs = (sgmnt_addrs *)0;					\
				cs_data = (sgmnt_data_ptr_t)0;					\
				break;								\
			default:								\
				assertpro(FALSE);						\
				break;								\
		}										\
	}											\
} MBEND

#define	TP_CHANGE_REG_IF_NEEDED(reg)								\
MBSTART {											\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;						\
	assert(reg);										\
	if (reg != gv_cur_region)								\
	{											\
		gv_cur_region = reg;								\
		switch (REG_ACC_METH(reg))							\
		{										\
			case dba_mm:								\
			case dba_bg:								\
				assert(reg->open);						\
				cs_addrs = &FILE_INFO(gv_cur_region)->s_addrs;			\
				cs_data = cs_addrs->hdr;					\
				assert((&FILE_INFO(gv_cur_region)->s_addrs == cs_addrs)		\
					&& cs_addrs->hdr == cs_data);				\
				if (cs_addrs->jnlpool && (jnlpool != cs_addrs->jnlpool))	\
					jnlpool = cs_addrs->jnlpool;				\
				break;								\
			case dba_usr:								\
			case dba_cm:								\
				cs_addrs = (sgmnt_addrs *)0;					\
				cs_data = (sgmnt_data_ptr_t)0;					\
				break;								\
			default:								\
				assertpro(FALSE);						\
				break;								\
		}										\
	}											\
} MBEND

#define PUSH_GV_CUR_REGION(REG, SAV_REG, SAV_CS_ADDRS, SAV_CS_DATA, SAV_JNLPOOL)		\
MBSTART {											\
	SAV_REG = gv_cur_region;								\
	SAV_CS_ADDRS = cs_addrs;								\
	SAV_CS_DATA = cs_data;									\
	SAV_JNLPOOL = jnlpool;									\
	TP_CHANGE_REG(REG);									\
} MBEND

#define POP_GV_CUR_REGION(SAV_REG, SAV_CS_ADDRS, SAV_CS_DATA, SAV_JNLPOOL)			\
MBSTART {											\
	gv_cur_region = SAV_REG;								\
	cs_addrs = SAV_CS_ADDRS;								\
	cs_data = SAV_CS_DATA;									\
	jnlpool = SAV_JNLPOOL;									\
} MBEND

/* The TP_TEND_CHANGE_REG macro is a special macro used in tp_tend.c to optimize out the unnecessary checks in
 * the TP_CHANGE_REG_IF_NEEDED macro. Also it sets cs_addrs and cs_data to precomputed values instead of recomputing
 * them from the region by dereferencing through a multitude of pointers. It does not check if gv_cur_region is
 * different from the input region. It assumes it is different enough % of times that the cost of the if check
 * is not worth the additional unconditional sets.
 */
#define	TP_TEND_CHANGE_REG(si)				\
MBSTART {						\
	gv_cur_region = si->gv_cur_region;		\
	cs_addrs = si->tp_csa;				\
	cs_data = si->tp_csd;				\
} MBEND

#define	GTCM_CHANGE_REG(reghead)										\
MBSTART {													\
	GBLREF cm_region_head	*curr_cm_reg_head;								\
	GBLREF gd_region	*gv_cur_region;									\
	GBLREF sgmnt_data	*cs_data;									\
	GBLREF sgmnt_addrs	*cs_addrs;									\
														\
	curr_cm_reg_head = (reghead);										\
	gv_cur_region = curr_cm_reg_head->reg;									\
	assert(IS_REG_BG_OR_MM(gv_cur_region));									\
	cs_addrs = &FILE_INFO(gv_cur_region)->s_addrs;								\
	cs_data = cs_addrs->hdr;										\
} MBEND

/* Macro to be used whenever cr->data_invalid needs to be set */
#define	SET_DATA_INVALID(cr)										\
MBSTART {												\
	uint4	in_tend, data_invalid;									\
													\
	DEBUG_ONLY(in_tend = cr->in_tend);								\
	DEBUG_ONLY(data_invalid = cr->data_invalid);							\
	assert((process_id == in_tend) || (0 == in_tend) && (0 == data_invalid));			\
	assert((0 == in_tend)										\
		|| (process_id == in_tend) && ((0 == data_invalid) || (process_id == data_invalid)));	\
	cr->data_invalid = process_id;									\
} MBEND

/* Macro to be used whenever cr->data_invalid needs to be re-set */
#define	RESET_DATA_INVALID(cr)				\
MBSTART {						\
	uint4	data_invalid;				\
							\
	DEBUG_ONLY(data_invalid = cr->data_invalid);	\
	assert(process_id == data_invalid);		\
	cr->data_invalid = 0;				\
} MBEND

/* Macro to be used whenever cr->in_cw_set needs to be set (PIN) inside a TP transaction */
#define	TP_PIN_CACHE_RECORD(cr, si)					\
MBSTART {								\
	assert(0 <= si->cr_array_index);				\
	assert(si->cr_array_index < si->cr_array_size);			\
	PIN_CACHE_RECORD(cr, si->cr_array, si->cr_array_index);		\
} MBEND

GBLREF	cache_rec_ptr_t	pin_fail_cr;			/* Pointer to the cache-record that we failed while pinning */
GBLREF	cache_rec	pin_fail_cr_contents;		/* Contents of the cache-record that we failed while pinning */
GBLREF	cache_rec_ptr_t	pin_fail_twin_cr;		/* Pointer to twin of the cache-record that we failed to pin */
GBLREF	cache_rec	pin_fail_twin_cr_contents;	/* Contents of twin of the cache-record that we failed to pin */
GBLREF	bt_rec_ptr_t	pin_fail_bt;			/* Pointer to bt of the cache-record that we failed to pin */
GBLREF	bt_rec		pin_fail_bt_contents;		/* Contents of bt of the cache-record that we failed to pin */
GBLREF	int4		pin_fail_in_crit;		/* Holder of crit at the time we failed to pin */
GBLREF	int4		pin_fail_wc_in_free;		/* Number of write cache records in free queue when we failed to pin */
GBLREF	int4		pin_fail_wcs_active_lvl;	/* Number of entries in active queue when we failed to pin */
GBLREF	int4		pin_fail_ref_cnt;		/* Reference count when we failed to pin */
GBLREF	int4		pin_fail_in_wtstart;		/* Count of processes in wcs_wtstart when we failed to pin */
GBLREF	int4		pin_fail_phase2_commit_pidcnt;	/* Number of processes in phase2 commit when we failed to pin */
/* Macro to be used whenever cr->in_cw_set needs to be set (PIN) outside of a TP transaction */
#define	PIN_CACHE_RECORD(cr, crarray, crarrayindex)							\
MBSTART {												\
	uint4	in_tend, data_invalid, in_cw_set;							\
													\
	DEBUG_ONLY(in_tend = cr->in_tend);								\
	DEBUG_ONLY(data_invalid = cr->data_invalid);							\
	assert((process_id == in_tend) || (0 == in_tend));						\
	assert((process_id == data_invalid) || (0 == data_invalid));					\
	in_cw_set = cr->in_cw_set;									\
	if (0 != in_cw_set)										\
	{												\
		pin_fail_cr = cr;									\
		pin_fail_cr_contents = *cr;								\
		if (cr->bt_index)									\
		{											\
			pin_fail_bt = (bt_rec_ptr_t)GDS_ANY_REL2ABS(cs_addrs, cr->bt_index);		\
			pin_fail_bt_contents = *pin_fail_bt;						\
		}											\
		if (cr->twin)										\
		{											\
			pin_fail_twin_cr = (cache_rec_ptr_t)GDS_ANY_REL2ABS(cs_addrs, cr->twin);	\
			pin_fail_twin_cr_contents = *pin_fail_twin_cr;					\
		}											\
		pin_fail_in_crit		= cs_addrs->nl->in_crit;				\
		pin_fail_wc_in_free		= cs_addrs->nl->wc_in_free;				\
		pin_fail_wcs_active_lvl		= cs_addrs->nl->wcs_active_lvl;				\
		pin_fail_ref_cnt		= cs_addrs->nl->ref_cnt;				\
		pin_fail_in_wtstart		= cs_addrs->nl->in_wtstart;				\
		pin_fail_phase2_commit_pidcnt	= cs_addrs->nl->wcs_phase2_commit_pidcnt;		\
		assertpro(0 == in_cw_set);								\
	}												\
	/* If twinning, we should never set in_cw_set on an OLDER twin. Assert that. */			\
	assert(!cr->twin || cr->bt_index);								\
	/* stuff it in the array before setting in_cw_set */						\
	crarray[crarrayindex] = cr;									\
	crarrayindex++;											\
	cr->in_cw_set = process_id;									\
} MBEND

/* Macro to be used whenever cr->in_cw_set needs to be re-set (UNPIN) in TP or non-TP) */
#define	UNPIN_CACHE_RECORD(cr)								\
MBSTART {										\
	uint4	in_tend, data_invalid, in_cw_set;					\
											\
	in_cw_set = cr->in_cw_set;							\
	if (process_id == cr->in_cw_set) /* reset in_cw_set only if we hold it */	\
	{										\
		DEBUG_ONLY(in_tend = cr->in_tend);					\
		DEBUG_ONLY(data_invalid = cr->data_invalid);				\
		assert((process_id == in_tend) || (0 == in_tend));			\
		assert((process_id == data_invalid) || (0 == data_invalid));		\
		cr->in_cw_set = 0;							\
	}										\
} MBEND

/* Macro to reset cr->in_cw_set for the entire cr_array in case of a retry (TP or non-TP) */
#define	UNPIN_CR_ARRAY_ON_RETRY(crarray, crarrayindex)				\
MBSTART {									\
	int4			lcl_crarrayindex;				\
	cache_rec_ptr_ptr_t	cr_ptr;						\
	cache_rec_ptr_t		cr;						\
	uint4			in_tend, data_invalid, in_cw_set;		\
										\
	lcl_crarrayindex = crarrayindex;					\
	if (lcl_crarrayindex)							\
	{									\
		cr_ptr = (cache_rec_ptr_ptr_t)&crarray[lcl_crarrayindex-1];	\
		while (lcl_crarrayindex--)					\
		{								\
			cr = *cr_ptr;						\
			DEBUG_ONLY(in_tend = cr->in_tend);			\
			DEBUG_ONLY(data_invalid = cr->data_invalid);		\
			DEBUG_ONLY(in_cw_set = cr->in_cw_set);			\
			assert(!data_invalid);					\
			assert(!in_tend);					\
			assert(process_id == in_cw_set);			\
			UNPIN_CACHE_RECORD(cr);					\
			cr_ptr--;						\
		}								\
		crarrayindex = 0;						\
	}									\
} MBEND

#define	RESET_CR_IN_TEND_AFTER_PHASE2_COMMIT(CR, CSA, CSD)									\
{																\
	cache_rec_ptr_t			cr_old;											\
	DEBUG_ONLY(cache_rec		lcl_cr_curr);										\
	DEBUG_ONLY(cache_rec		lcl_cr_old);										\
																\
	GBLREF	uint4		process_id;											\
																\
	if (CR->backup_cr_is_twin)												\
	{	/* We created a twin in "bg_update_phase1". If we had pinned the older twin (Note: it is possible in some cases	\
		 * that we do not pin the older twin (e.g. in tp_tend if cse->new_buff is non-NULL), let us unpin the older twin\
		 * before unpinning the newer twin. Not doing so could cause "db_csh_getn" to identify the new twin as a buffer	\
		 * ready for replacement but "wcs_get_space/wcs_wtstart_fini/wcs_wtstart/wcs_wtfini" would not be able to flush	\
		 * the new twin until the old twin has its "in_cw_set" cleared resulting in a livelock.				\
		 */														\
		assert(TWINNING_ON(CSD));											\
		cr_old = (cache_rec_ptr_t)GDS_ANY_REL2ABS(CSA, CR->twin);	/* get old twin */				\
		DEBUG_ONLY(lcl_cr_old = *cr_old);	/* In case the following assert trips capture the CR contents */	\
		DEBUG_ONLY(lcl_cr_curr = *CR);											\
		assert(!cr_old->bt_index);											\
		assert(!cr_old->in_cw_set || cr_old->dirty);									\
		assert(CR->bt_index);												\
		assert((process_id == cr_old->in_cw_set) || (0 == cr_old->in_cw_set));						\
		UNPIN_CACHE_RECORD(cr_old);											\
	}															\
	/* A concurrent process reading this block will wait for in_tend to become FALSE and then proceed with its		\
	 * database operation. Later it will reach t_end/tp_tend doing validations at which point it will need to set in_cw_set.\
	 * It expects in_cw_set to be 0 at that point. Therefore in_cw_set needs to be reset to 0 BEFORE resetting in_tend.	\
	 * Need a write memory barrier to ensure that these two updates are seen in that order by any other concurrent process.	\
	 */															\
	assert(process_id == CR->in_cw_set);											\
	UNPIN_CACHE_RECORD(CR);													\
	assert(!CR->in_cw_set);													\
	SHM_WRITE_MEMORY_BARRIER;												\
	assert(process_id == CR->in_tend);	/* should still be valid */							\
	CR->in_tend = 0;													\
}

/* Macro to check that UNPIN of cr is complete (i.e. cr->in_cw_set has been reset to 0) for the entire cr_array in case of a
 * commit (TP or non-TP). Usually in_cw_set is set for all cache-records that we are planning on updating before we start phase1.
 * After updating each cse in phase2, we reset the corresponding cse->cr->in_cw_set. Therefore on a successful commit, after
 * completing all cses in phase2, we don't expect any pinned cr->in_cw_set at all.
 */
#ifdef DEBUG
#define ASSERT_CR_ARRAY_IS_UNPINNED(CSD, CRARRAY, CRARRAYINDEX)				\
MBSTART {										\
	GBLREF uint4		process_id;						\
											\
	int4			lcl_crarrayindex;					\
	cache_rec_ptr_ptr_t	cr_ptr, cr_start;					\
	cache_rec_ptr_t		cr;							\
											\
	lcl_crarrayindex = CRARRAYINDEX;						\
	if (lcl_crarrayindex)								\
	{										\
		cr_ptr = (cache_rec_ptr_ptr_t)&CRARRAY[lcl_crarrayindex];		\
		cr_start = &CRARRAY[0];							\
		while (--cr_ptr >= cr_start)						\
		{									\
			cr = *cr_ptr;							\
			assert(process_id != cr->in_cw_set);				\
		}									\
	}										\
} MBEND
#else
#define ASSERT_CR_ARRAY_IS_UNPINNED(CSD, CRARRAY, CRARRAYINDEX)
#endif

/* Every process that initializes journal pool has two aspects of validation.
 * 1. 	Validate whether this process can do logical updates to the journal pool that is initialized. If not, the process should
 *	issue SCNDDBNOUPD error. Examples of this would be a GT.M update that happens on a non-supplementary Receiver Side.
 *
 * 2.	Validate whether the process is attached to the correct journal pool. If not, REPLINSTMISMTCH should be issued. This check
 *	ensures that we only do updates to region that is tied to the journal pool that we initialized.
 *
 * The below macro definitions indicate which of the above 2 checks is done by the process for a particular region until now.
 * The macro VALIDATE_INITIALIZED_JNLPOOL does the actual validation.
 */
#define JNLPOOL_NOT_VALIDATED		0x00
#define REPLINSTMISMTCH_CHECK_DONE	0x01
#define SCNDDBNOUPD_CHECK_DONE		0x02
#define JNLPOOL_VALIDATED		0x03

#define SCNDDBNOUPD_CHECK_FALSE		0x00
#define SCNDDBNOUPD_CHECK_TRUE		0x01

#define VALIDATE_INITIALIZED_JNLPOOL(CSA, CNL, REG, JNLPOOL_USER, SCNDDBNOUPD_CHECK_NEEDED)					\
MBSTART {															\
	GBLREF	boolean_t		is_updproc;										\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;										\
																\
	unsigned char			instfilename_copy[MAX_FN_LEN + 1];							\
	sm_uc_ptr_t			jnlpool_instfilename;									\
	int4				jnlpool_shmid;										\
	uint4				jnlpool_validate_check;									\
	boolean_t			do_REPLINSTMISMTCH_check;								\
																\
	jnlpool_validate_check = CSA->jnlpool_validate_check;									\
	assert(JNLPOOL_VALIDATED >= jnlpool_validate_check);									\
	if (JNLPOOL_VALIDATED != jnlpool_validate_check)									\
	{															\
		do_REPLINSTMISMTCH_check = (!(REPLINSTMISMTCH_CHECK_DONE & jnlpool_validate_check) 				\
						&& ((GTMRELAXED != JNLPOOL_USER) || !IS_GTM_IMAGE));				\
		if (!(SCNDDBNOUPD_CHECK_DONE & jnlpool_validate_check) && SCNDDBNOUPD_CHECK_NEEDED)				\
		{														\
			if (jnlpool->jnlpool_ctl->upd_disabled && !is_updproc)							\
			{	/* Updates are disabled in this journal pool. Issue error. Do NOT detach from journal pool	\
				 * as that would cause us not to honor instance freeze (in case ydb_custom_errors env var is	\
				 * non-null) for database reads that this process later does (for example reading a block	\
				 * might require us to flush a dirty buffer to disk which should pause if the instance is	\
				 * frozen).					 						\
				 */												\
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_SCNDDBNOUPD);					\
			}													\
			CSA->jnlpool_validate_check |= SCNDDBNOUPD_CHECK_DONE;							\
		}														\
		if (do_REPLINSTMISMTCH_check)											\
		{														\
			jnlpool_instfilename = (sm_uc_ptr_t)jnlpool->jnlpool_ctl->jnlpool_id.instfilename;			\
			if (STRCMP(CNL->replinstfilename, jnlpool_instfilename)							\
				|| (CNL->jnlpool_shmid != jnlpool->repl_inst_filehdr->jnlpool_shmid))				\
			{													\
				/* Replication instance filename or jnlpool shmid mismatch. Two possibilities.			\
				 * (a) Database has already been bound with a replication instance file name that is different	\
				 *	from the instance file name used by the current process.				\
				 * (b) Database has already been bound with a jnlpool shmid and another jnlpool is about to	\
				 *	be bound with the same database. Disallow this mixing of multiple jnlpools.		\
				 * Issue error. But before that detach from journal pool.					\
				 * Copy replication instance file name in journal pool to temporary memory before detaching.	\
				 * Actually case (b) subsumes (a) so we assert that below. But in pro we handle both cases	\
				 *	just in case.										\
				 */												\
				assert(CNL->jnlpool_shmid != jnlpool->repl_inst_filehdr->jnlpool_shmid);			\
				assert(SIZEOF(instfilename_copy) == SIZEOF(jnlpool->jnlpool_ctl->jnlpool_id.instfilename));	\
				memcpy(&instfilename_copy[0], jnlpool_instfilename, SIZEOF(instfilename_copy));			\
				assert(SIZEOF(jnlpool_shmid) == SIZEOF(CNL->jnlpool_shmid));					\
				jnlpool_shmid = jnlpool->repl_inst_filehdr->jnlpool_shmid;					\
				assert(NULL != jnlpool->jnlpool_ctl);								\
				if (INVALID_SHMID == CNL->jnlpool_shmid)							\
					rts_error_csa(CSA_ARG(CSA) VARLSTCNT(4) ERR_REPLINSTNOSHM, 2, DB_LEN_STR(REG));		\
				else												\
					rts_error_csa(CSA_ARG(CSA) VARLSTCNT(10) ERR_REPLINSTMISMTCH, 8,			\
						LEN_AND_STR(instfilename_copy), jnlpool_shmid, DB_LEN_STR(REG),			\
						LEN_AND_STR(CNL->replinstfilename), CNL->jnlpool_shmid);			\
			}													\
			CSA->jnlpool_validate_check |= REPLINSTMISMTCH_CHECK_DONE;						\
			CSA->jnlpool = jnlpool;											\
		}														\
	}															\
} MBEND

#define	JNLPOOL_INIT_IF_NEEDED(CSA, CSD, CNL, SCNDDBNOUPD_CHECK_NEEDED)						\
MBSTART {													\
	GBLREF	boolean_t		is_replicator;								\
	GBLREF	gd_region		*gv_cur_region;								\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool_head;								\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;								\
														\
	gd_addr				*gdPtr;									\
														\
	if (REPL_ALLOWED(CSD) && is_replicator)									\
	{													\
		jnlpool_addrs_ptr_t	lcl_jnlpool;								\
														\
		lcl_jnlpool = (jnlpool_addrs_ptr_t)CSA->jnlpool;						\
		gdPtr = CSA->gd_ptr;										\
		if (NULL == lcl_jnlpool)									\
			lcl_jnlpool = gdPtr->gd_runtime->jnlpool;						\
		if ((NULL == lcl_jnlpool) || !lcl_jnlpool->pool_init)						\
		{												\
			jnlpool_init((jnlpool_user)GTMPROC, (boolean_t)FALSE, (boolean_t *)NULL, gdPtr);	\
			assert((NULL != jnlpool) && jnlpool->pool_init);					\
		} else												\
			jnlpool = lcl_jnlpool;									\
		VALIDATE_INITIALIZED_JNLPOOL(CSA, CNL, gv_cur_region, GTMPROC, SCNDDBNOUPD_CHECK_NEEDED);	\
		assert(CSA->jnlpool == jnlpool);								\
		assert(gdPtr->gd_runtime->jnlpool == jnlpool);							\
	}													\
} MBEND

#define ASSERT_VALID_JNLPOOL(CSA)										\
MBSTART {													\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;								\
														\
	assert(CSA && CSA->critical && CSA->nl); /* should have been setup in mu_rndwn_replpool */		\
	assert(jnlpool && jnlpool->jnlpool_ctl);								\
	assert(CSA->critical == (mutex_struct_ptr_t)((sm_uc_ptr_t)jnlpool->jnlpool_ctl + JNLPOOL_CTL_SIZE));	\
	assert(CSA->nl == (node_local_ptr_t) ((sm_uc_ptr_t)CSA->critical + JNLPOOL_CRIT_SPACE			\
		+ SIZEOF(mutex_spin_parms_struct)));								\
	assert(jnlpool->jnlpool_ctl->filehdr_off);								\
	assert(jnlpool->jnlpool_ctl->srclcl_array_off > jnlpool->jnlpool_ctl->filehdr_off);			\
	assert(jnlpool->jnlpool_ctl->sourcelocal_array_off > jnlpool->jnlpool_ctl->srclcl_array_off);		\
	assert(jnlpool->repl_inst_filehdr == (repl_inst_hdr_ptr_t)((sm_uc_ptr_t)jnlpool->jnlpool_ctl		\
			+ jnlpool->jnlpool_ctl->filehdr_off));							\
	assert(jnlpool->gtmsrc_lcl_array == (gtmsrc_lcl_ptr_t)((sm_uc_ptr_t)jnlpool->jnlpool_ctl		\
			+ jnlpool->jnlpool_ctl->srclcl_array_off));						\
	assert(jnlpool->gtmsource_local_array == (gtmsource_local_ptr_t)((sm_uc_ptr_t)jnlpool->jnlpool_ctl	\
			+ jnlpool->jnlpool_ctl->sourcelocal_array_off));					\
} MBEND

/* Explanation for why we need the following macro.
 *
 * Normally a cdb_sc_blkmod check is done using the "bt". This is done in t_end and tp_tend.
 * But that is possible only if we hold crit. There are a few routines (TP only) that need
 * to do this check outside of crit (e.g. tp_hist, gvcst_search). For those, the following macro
 * is defined. This macro compares transaction numbers directly from the buffer instead of
 * going through the bt or blk queues. This is done to speed up processing. One consequence
 * is that we might encounter a situation where the buffer's contents hasn't been modified,
 * but the block might actually have been changed e.g. if asyncio=ON, a twin buffer might have been
 * created or the "blk" field in the cache-record corresponding to this buffer might have
 * been made CR_BLKEMPTY etc. In these cases, we rely on the fact that the cycle for the
 * buffer would have been incremented thereby saving us in the cdb_sc_lostcr check which will
 * always FOLLOW (never PRECEDE) this check.
 *
 * Note that in case of BG, it is possible that the buffer could be in the process of being updated
 * (phase2 outside of crit). In this case we have to restart as otherwise we could incorrectly
 * validate an inconsistent state of the database as okay. For example, say our search path
 * contains a level-1 root-block and a level-0 data block. If both of these blocks were
 * concurrently being updated in phase2 (outside of crit) by another process, it is possible
 * (because of the order in which blocks are committed) that the data block contents get
 * modified first but the index block is still unchanged. If we traversed down the tree at
 * this instant, we are looking at a search path that contains a mix of pre-update and post-update
 * blocks and should never validate this traversal as okay. In this case, the cache record
 * corresponding to the index block would have its "in_tend" flag non-zero indicating update is pending.
 *
 * The order of the check should be cr->in_tend BEFORE the buffaddr->tn check. Doing it otherwise
 * would mean it is posible for the buffaddr->tn check to succeed and before the cr->in_tend
 * check is done the buffer gets rebuilt (from start to finish in phase2). This would result
 * in us falsely validating this transaction as okay when in fact we should have restarted.
 *
 * Because we rely on the fact that cr->in_tend is reset to 0 AFTER t1->buffaddr->tn is updated, and
 * since these could be updated concurrently, and since this macro is used outside of crit, we need to
 * ensure a read memory barrier is done. Currently, the only two places which use this macro are tp_hist.c
 * and gvcst_search.c. Out of this, the latter uses this only as a performance measure and not for correctness.
 * But the former uses this for correctness. In fact tp_tend.c relies on tp_hist.c doing a proper validation.
 * Therefore the read memory barrier is essential in tp_hist.c and not needed in gvcst_search.c. See tp_hist.c
 * for use of the read memory barrier and a comment describing why it is ok to do it only once per function
 * invocation (instead of using it once per block that gets validated).
 *
 * There are two variants of this macro.
 * TP_IS_CDB_SC_BLKMOD  : That calculates the blktn by doing t1->buffaddr->tn explicitly.
 * TP_IS_CDB_SC_BLKMOD3 : This is provided the blktn as input so can avoid the explicit calculation.
 */
#define	TP_IS_CDB_SC_BLKMOD(cr, t1) (((NULL != (cr)) && (cr)->in_tend) || ((t1)->tn <= ((blk_hdr_ptr_t)(t1)->buffaddr)->tn))
#define	TP_IS_CDB_SC_BLKMOD3(cr, t1, blktn) (((NULL != (cr)) && (cr)->in_tend) || ((t1)->tn <= blktn))

#define MM_ADDR(SGD)		((sm_uc_ptr_t)(((sgmnt_data_ptr_t)SGD) + 1))
#define MASTER_MAP_BLOCKS_DFLT		496				/* 496 gives 992M possible blocks */
#define MASTER_MAP_BLOCKS_V5		112				/* 112 gives 224M possible blocks */
#define MASTER_MAP_BLOCKS_V4		32				/* 32 gives 64M possible blocks  */
#define MASTER_MAP_BLOCKS_MAX		MASTER_MAP_BLOCKS_DFLT		/* 496 gives 992M possible blocks  */
#define MASTER_MAP_BLOCKS_V5_OLD	64				/* V5 database previous master map block size */
#define MASTER_MAP_SIZE_V5_OLD	(MASTER_MAP_BLOCKS_V5_OLD * DISK_BLOCK_SIZE)
#define MASTER_MAP_SIZE_V4	(MASTER_MAP_BLOCKS_V4 * DISK_BLOCK_SIZE)	/* MUST be a multiple of DISK_BLOCK_SIZE */
#define MASTER_MAP_SIZE_MAX	(MASTER_MAP_BLOCKS_MAX * DISK_BLOCK_SIZE)	/* MUST be a multiple of DISK_BLOCK_SIZE */
#define MASTER_MAP_SIZE_DFLT	(MASTER_MAP_BLOCKS_DFLT * DISK_BLOCK_SIZE)	/* MUST be a multiple of DISK_BLOCK_SIZE */
#define MASTER_MAP_SIZE_V5	(MASTER_MAP_BLOCKS_V5 * DISK_BLOCK_SIZE)	/* MUST be a multiple of DISK_BLOCK_SIZE */
#define MASTER_MAP_SIZE(SGD)	(((sgmnt_data_ptr_t)SGD)->master_map_len)
#define SGMNT_HDR_LEN		SIZEOF(sgmnt_data)
#define SIZEOF_FILE_HDR(SGD)	(SGMNT_HDR_LEN + MASTER_MAP_SIZE(SGD))
#define SIZEOF_FILE_HDR_DFLT	(SGMNT_HDR_LEN + MASTER_MAP_SIZE_DFLT)
#define SIZEOF_FILE_HDR_V5	(SGMNT_HDR_LEN + MASTER_MAP_SIZE_V5)
#define SIZEOF_FILE_HDR_MIN	(SGMNT_HDR_LEN + MASTER_MAP_SIZE_V4)
#define SIZEOF_FILE_HDR_MAX	(SGMNT_HDR_LEN + MASTER_MAP_SIZE_MAX)
#define MM_BLOCK		(SGMNT_HDR_LEN / DISK_BLOCK_SIZE + 1)	/* gt.m numbers blocks from 1 */
#define TH_BLOCK	 	1

#define JNL_NAME_SIZE	        256  /* possibly expanded when opened. Macro value should not change as it is used in db file hdr */
#define JNL_NAME_EXP_SIZE	1024 /* MAXPATHLEN, before jnl_buffer in shared memory */

#define BLKS_PER_LMAP		512
#define MAXTOTALBLKS_V4		(MASTER_MAP_SIZE_V4 * 8 * BLKS_PER_LMAP)
#define MAXTOTALBLKS_V5		(MASTER_MAP_SIZE_MAX * 8 * BLKS_PER_LMAP)
#define MAXTOTALBLKS_V6		(MASTER_MAP_SIZE_MAX * 8 * BLKS_PER_LMAP)
#define MAXTOTALBLKS_MAX	(MASTER_MAP_SIZE_MAX * 8 * BLKS_PER_LMAP)
#define MAXTOTALBLKS(SGD)	(MASTER_MAP_SIZE(SGD) * 8 * BLKS_PER_LMAP)
#define	IS_BITMAP_BLK(blk)	(ROUND_DOWN2(blk, BLKS_PER_LMAP) == blk)	/* TRUE if blk is a bitmap */
/* 	V6 - 8K fileheader (= 16 blocks) + 248K mastermap (= 496 blocks) + 1
 * 	V5 - 8K fileheader (= 16 blocks) + 56K mastermap (= 112 blocks) + 1
 * 	V4 - 8K fileheader (= 16 blocks) + 16K mastermap (= 32 blocks) + 1
 */
#define START_VBN_V6		513
#define START_VBN_V5		129
#define	START_VBN_V4		 49
#define START_VBN_CURRENT	START_VBN_V6

#define	STEP_FACTOR			64		/* the factor by which flush_trigger is incremented/decremented */
#define	MIN_FLUSH_TRIGGER(n_bts)	((n_bts)/4)	/* the minimum flush_trigger as a function of n_bts */
#define	MAX_FLUSH_TRIGGER(n_bts)	((n_bts)*15/16)	/* the maximum flush_trigger as a function of n_bts */

#define MIN_FILLFACTOR 30
#define MAX_FILLFACTOR 100

#ifdef DEBUG_DYNGRD
#  define DEBUG_DYNGRD_ONLY(X) X
#else
#  define DEBUG_DYNGRD_ONLY(X)
#endif

#define SAVE_WTSTART_PID(cnl, pid, index)			\
MBSTART {							\
	for (index = 0; index < MAX_WTSTART_PID_SLOTS; index++)	\
		if (0 == cnl->wtstart_pid[index])		\
			break;					\
	if (MAX_WTSTART_PID_SLOTS > index)			\
		cnl->wtstart_pid[index] = pid;			\
} MBEND

#define CLEAR_WTSTART_PID(cnl, index)				\
MBSTART {							\
	if (MAX_WTSTART_PID_SLOTS > index)			\
		cnl->wtstart_pid[index] = 0;			\
} MBEND

#define	WRITERS_ACTIVE(cnl)	((0 < cnl->intent_wtstart) || (0 < cnl->in_wtstart))

#define	SIGNAL_WRITERS_TO_STOP(cnl)								\
MBSTART {											\
	SET_TRACEABLE_VAR((cnl)->wc_blocked, TRUE);	/* to stop all active writers */	\
	/* memory barrier needed to broadcast this information to other processors */		\
	SHM_WRITE_MEMORY_BARRIER;								\
} MBEND

#define	WAIT_FOR_WRITERS_TO_STOP(cnl, lcnt, maxiters)						\
MBSTART {	/* We need to ensure that an uptodate value of cnl->intent_wtstart is read in the\
	 * WRITERS_ACTIVE macro every iteration of the loop hence the read memory barrier.	\
	 */											\
	SHM_READ_MEMORY_BARRIER;								\
	for (lcnt=1; WRITERS_ACTIVE(cnl) && (lcnt <= maxiters);  lcnt++)			\
	{	/* wait for any processes INSIDE or at ENTRY of wcs_wtstart to finish */	\
		wcs_sleep(lcnt);								\
		SHM_READ_MEMORY_BARRIER;							\
	}											\
} MBEND

#define	SIGNAL_WRITERS_TO_RESUME(cnl)								\
MBSTART {											\
	SET_TRACEABLE_VAR((cnl)->wc_blocked, FALSE); /* to let active writers resume */	\
	/* memory barrier needed to broadcast this information to other processors */		\
	SHM_WRITE_MEMORY_BARRIER;								\
} MBEND

#define	INCR_INTENT_WTSTART(cnl)									\
MBSTART {												\
	INCR_CNT(&cnl->intent_wtstart, &cnl->wc_var_lock); /* signal intent to enter wcs_wtstart */	\
	if (0 >= cnl->intent_wtstart)									\
	{	/* possible if wcs_verify had reset this flag */					\
		INCR_CNT(&cnl->intent_wtstart, &cnl->wc_var_lock);					\
		/* wcs_verify cannot possibly have reset this flag again because it does this only	\
		 * after wcs_recover waits for a maximum of 1 minute (for this flag to become zero)	\
		 * before giving up. Therefore for that to happen, we should have been context		\
		 * switched out for 1 minute after the second INCR_CNT but before the below assert)	\
		 * We believe that is an extremely unlikely condition so don't do anything about it.	\
		 * In the worst case this will get reset to 0 by the next wcs_verify or INCR_CNT	\
		 * (may need multiple INCR_CNTs depending on how negative a value this is) whichever	\
		 * happens sooner.									\
		 */											\
		assert(0 < cnl->intent_wtstart);							\
	}												\
} MBEND

#define	DECR_INTENT_WTSTART(cnl)					\
MBSTART {								\
	if (0 < cnl->intent_wtstart)					\
		DECR_CNT(&cnl->intent_wtstart, &cnl->wc_var_lock);	\
	/* else possible if wcs_verify had reset this flag */		\
} MBEND

#define ENSURE_JNL_OPEN(csa, reg)                                                 					\
MBSTART {                                                                               				\
	boolean_t		was_crit;                                               				\
	jnl_private_control	*jpc;											\
	sgmnt_data_ptr_t	csd;											\
	uint4			jnl_status;										\
															\
	assert(cs_addrs == csa);                                                        				\
	assert(gv_cur_region == reg);                                                   				\
	assert(FALSE == reg->read_only);                                                				\
	csd = csa->hdr;													\
	if (JNL_ENABLED(csd))												\
	{                                                                       					\
		was_crit = csa->now_crit;                                         					\
		if (!was_crit)                                                  					\
			grab_crit(reg);                                         					\
		jnl_status = JNL_ENABLED(csd) ? jnl_ensure_open(reg, csa) : 0;    	                             	\
		if (!was_crit)                                                  					\
			rel_crit(reg);                                          					\
		if (0 != jnl_status)											\
		{													\
			jpc = csa->jnl;											\
			assert(NULL != jpc);										\
			if (SS_NORMAL != jpc->status)									\
				rts_error_csa(CSA_ARG(csa) VARLSTCNT(7) jnl_status, 4, JNL_LEN_STR(csd),		\
						DB_LEN_STR(gv_cur_region), jpc->status);				\
			else												\
				rts_error_csa(CSA_ARG(csa) VARLSTCNT(6) jnl_status, 4, JNL_LEN_STR(csd),		\
						DB_LEN_STR(gv_cur_region));						\
		}													\
	}                                                                       					\
} MBEND

#define JNL_ENSURE_OPEN_WCS_WTSTART(csa, reg, num_bufs, cr2flush, CHILLWAIT, RET)		\
MBSTART {       										\
	if ((CHILLWAIT) && !FREEZE_LATCH_HELD(csa))						\
		WAIT_FOR_REGION_TO_UNCHILL(csa, csa->hdr);					\
	ENSURE_JNL_OPEN(csa, reg);                              				\
	if (csa->hdr->asyncio)									\
		RET = wcs_wtstart_fini(reg, num_bufs, cr2flush);				\
	else											\
		RET = wcs_wtstart(reg, num_bufs, NULL, cr2flush);				\
} MBEND

/* Macros to effect changes in the blks_to_upgrd field of the file-header.
 * We should hold crit on the region in all cases except for one when we are in MUPIP CREATE (but we are still standalone here).
 * Therefore we need not use any interlocks to update this field. This is asserted below.
 * Although we can derive "csd" from "csa", we pass them as two separate arguments for performance reasons.
 * Use local variables to record shared memory information doe debugging purposes in case of an assert failure.
 */
#define INCR_BLKS_TO_UPGRD(csa, csd, delta)						\
MBSTART {										\
	int4	new_blks_to_upgrd;							\
	int4	cur_blks_to_upgrd;							\
	int4	cur_delta;								\
											\
	assert((csd)->createinprogress || (csa)->now_crit);				\
	cur_delta = (delta);								\
	assert((csa)->hdr == (csd));							\
	assert(0 != cur_delta);								\
	cur_blks_to_upgrd = (csd)->blks_to_upgrd;					\
	assert(0 <= (csd)->blks_to_upgrd);						\
	new_blks_to_upgrd = cur_delta + cur_blks_to_upgrd;				\
	assert(0 <= new_blks_to_upgrd);							\
	(csd)->blks_to_upgrd = new_blks_to_upgrd;					\
	if (0 >= new_blks_to_upgrd)							\
	{										\
		if (0 == new_blks_to_upgrd)						\
			(csd)->tn_upgrd_blks_0 = (csd)->trans_hist.curr_tn;		\
		else									\
		{	/* blks_to_upgrd counter in the fileheader should never hold a	\
			 * negative value. Note down the negative value in a separate	\
			 * field for debugging and set the counter to 0.		\
			 */								\
			(csd)->blks_to_upgrd = 0;					\
			(csd)->blks_to_upgrd_subzero_error -= (new_blks_to_upgrd);	\
		}									\
	} else										\
		(csd)->fully_upgraded = FALSE;						\
} MBEND
#define DECR_BLKS_TO_UPGRD(csa, csd, delta)	INCR_BLKS_TO_UPGRD((csa), (csd), -(delta))

/* Interlocked queue instruction constants ... */

#define	QI_STARVATION		3
#define EMPTY_QUEUE		0L
#define QUEUE_WAS_EMPTY		1
#define INTERLOCK_FAIL		-1L
#define QUEUE_INSERT_SUCCESS	1

typedef trans_num	bg_trc_rec_tn;
typedef int4		bg_trc_rec_cntr;

typedef struct
{
	int4	curr_count;	/* count for this invocation of shared memory */
	int4	cumul_count;	/* count from the creation of database (not including this invocation) */
} db_csh_acct_rec;

#define	TAB_DB_CSH_ACCT_REC(A,B,C)	A,
enum db_csh_acct_rec_type
{
#include "tab_db_csh_acct_rec.h"
n_db_csh_acct_rec_types
};
#undef TAB_DB_CSH_ACCT_REC

#include "gvstats_rec.h"
#include "probecrit_rec.h"

#define	GVSTATS_SET_CSA_STATISTIC(CSA, COUNTER, VALUE)							\
MBSTART {												\
	if (0 == (RDBF_NOSTATS & (CSA)->reservedDBFlags))						\
		(CSA)->gvstats_rec_p->COUNTER = VALUE;							\
} MBEND

#define	INCR_GVSTATS_COUNTER(CSA, CNL, COUNTER, INCREMENT)						\
MBSTART {												\
	if (0 == (RDBF_NOSTATS & (CSA)->reservedDBFlags))						\
	{												\
		(CSA)->gvstats_rec_p->COUNTER += INCREMENT;		/* private or shared stats */	\
		(CNL)->gvstats_rec.COUNTER += INCREMENT;		/* database stats */ 		\
	}												\
} MBEND

#define	SYNC_RESERVEDDBFLAGS_REG_CSA_CSD(REG, CSA, CSD, CNL)								\
MBSTART {														\
	uint4			reservedDBFlags;									\
															\
	if (TREF(ok_to_see_statsdb_regs))										\
	{														\
		reservedDBFlags = CSD->reservedDBFlags;	/* sgmnt_data is flag authority */				\
		/* If this is a base DB (i.e. not a statsdb), but we could not successfully create the statsdb		\
		 * (e.g. $ydb_statsdir issues etc.) then disable RDBF_STATSDB in the region. So this db continues	\
		 * without statistics gathering.									\
		 */													\
		if (!IS_RDBF_STATSDB(CSD) && (NULL != CNL) && !CNL->statsdb_fname_len)					\
			reservedDBFlags &= (~RDBF_STATSDB);								\
		REG->reservedDBFlags = CSA->reservedDBFlags = reservedDBFlags;						\
	}														\
	/* else : "gd_load" would have already set RDBF_NOSTATS etc. so don't override that */				\
} MBEND

/* Clear everything from the end of the encryption section up to the ntrpt_recov_resync_strm_seqno array, which includes all
 * bg_trc_rec_tn, all bg_trc_rec_cntr, all db_csh_acct_rec, all gvstats as well as any intervening filler; also clear
 * tp_cdb_sc_blkmod.
 *
 * This means we MUST NOT insert anything in the file header between the encryption section and intrpt_recov_resync_strm_seqno
 * nor move either of those end points without appropriately adjusting this macro.
 */
#define	CLRGVSTATS(CSA)									\
MBSTART {										\
	char			*CHPTR;							\
	int			CLRLEN;							\
	sgmnt_data_ptr_t	CSD;							\
											\
	CSD = CSA->hdr;									\
	CHPTR = (char *)CSD->filler_encrypt + SIZEOF(CSD->filler_encrypt);		\
	CLRLEN = (char *)&CSD->intrpt_recov_resync_strm_seqno - CHPTR;			\
	memset(CHPTR, 0, CLRLEN);							\
	gvstats_rec_csd2cnl(CSA);	/* we update gvstats in cnl */			\
	memset((char *)&CSD->tp_cdb_sc_blkmod, 0, SIZEOF(CSD->tp_cdb_sc_blkmod));	\
} MBEND

#if defined(DEBUG) || defined(DEBUG_DB_CSH_COUNTER)
#	define	INCR_DB_CSH_COUNTER(csa, counter, increment)				\
			if (csa->read_write || dba_bg == csa->hdr->acc_meth)		\
				csa->hdr->counter.curr_count += increment;
#else
#	define	INCR_DB_CSH_COUNTER(csa, counter, increment)
#endif

enum tp_ntp_blkmod_type		/* used for accounting in cs_data->tp_cdb_sc_blkmod[] */
{
	/* TP transactions */
	tp_blkmod_nomod = 0,
	tp_blkmod_gvcst_srch,
	tp_blkmod_t_qread,
	tp_blkmod_tp_tend,
	tp_blkmod_tp_hist,
	tp_blkmod_op_tcommit,
	n_tp_blkmod_types,
	/* NON-TP transactions */
	t_blkmod_nomod,
	t_blkmod_gvcst_srch,
	t_blkmod_gvcst_expand_key,
	t_blkmod_t_qread,
	t_blkmod_t_end1,
	t_blkmod_t_end2,
	t_blkmod_t_end3,
	t_blkmod_t_end4,
	/* MUPIP specific */
	t_blkmod_mu_clsce,
	t_blkmod_mu_reduce_level,
	t_blkmod_mu_split,
	t_blkmod_mu_swap_blk,
	t_blkmod_reorg_funcs,
	n_nontp_blkmod_types
};

/* Below is a list of macro bitmasks used to set the global variable "donot_commit". This variable should normally be 0.
 * But in rare cases, we could end up in situations where we know it is a restartable situation but decide not to
 * restart right away (because of interface issues that the function where this is detected cannot signal a restart
 * or because we don't want to take a performance hit to check this restartable situation in highly frequented code if
 * the restart will anyway be detected before commit. In this cases, this variable will take on non-zero values.
 * The commit logic will assert that this variable is indeed zero after validation but before proceeding with commit.
 */
#define	DONOTCOMMIT_TPHIST_BLKTARGET_MISMATCH		(1 << 0) /* Restartable situation seen in tp_hist */
#define	DONOTCOMMIT_GVCST_DELETE_BLK_CSE_TLEVEL		(1 << 1) /* Restartable situation seen in gvcst_delete_blk */
#define	DONOTCOMMIT_JNLGETCHECKSUM_NULL_CR		(1 << 2) /* Restartable situation seen in jnl_get_checksum.h */
#define	DONOTCOMMIT_GVCST_KILL_ZERO_TRIGGERS		(1 << 3) /* Restartable situation seen in gvcst_kill */
#define	DONOTCOMMIT_GVCST_BLK_BUILD_TPCHAIN		(1 << 4) /* Restartable situation seen in gvcst_blk_build */
#define	DONOTCOMMIT_T_QREAD_BAD_PVT_BUILD		(1 << 5) /* Restartable situation due to bad private build in t_qread */
#define	DONOTCOMMIT_GVCST_SEARCH_LEAF_BUFFADR_NOTSYNC	(1 << 6) /* Restartable situation seen in gvcst_search */
#define	DONOTCOMMIT_GVCST_SEARCH_BLKTARGET_MISMATCH	(1 << 7) /* Restartable situation seen in gvcst_search */
#define DONOTCOMMIT_GVCST_PUT_SPLIT_TO_RIGHT		(1 << 8) /* Restartable situation seen in gvcst_put */
#define DONOTCOMMIT_T_WRITE_CSE_DONE			(1 << 9) /* Restartable situation seen in t_write */
#define DONOTCOMMIT_T_WRITE_CSE_MODE			(1 << 10) /* Restartable situation seen in t_write */
#define DONOTCOMMIT_TRIGGER_SELECT_XECUTE		(1 << 11) /* Restartable situation seen in trigger_select */
#define DONOTCOMMIT_JNL_FORMAT				(1 << 12) /* Restartable situation seen in jnl_format */
#define DONOTCOMMIT_COPY_PREV_KEY_TO_GVT_CLUE		(1 << 13) /* Restartable situation seen in COPY_PREV_KEY_TO_GVT_CLUE */

#define TAB_BG_TRC_REC(A,B)	B,
enum bg_trc_rec_type
{
#include "tab_bg_trc_rec.h"
n_bg_trc_rec_types
};
#undef TAB_BG_TRC_REC

#define UPGRD_WARN_INTERVAL (60 * 60 * 24)	/* Once every 24 hrs */

/* The following structure is used to determine
   the endianess of a database header.
*/

typedef union
{
	struct {
		unsigned short little_endian;
		unsigned short big_endian;
		} shorts;
	uint4 word32;
} endian32_struct;

#ifdef BIGENDIAN
#  define ENDIANCHECKTHIS	big_endian
#else
#  define ENDIANCHECKTHIS	little_endian
#endif

#define CHECK_DB_ENDIAN(CSD,FNLEN,FNNAME)								\
MBSTART {												\
	endian32_struct	check_endian;									\
	check_endian.word32 = (CSD)->minor_dbver;							\
	if (!check_endian.shorts.ENDIANCHECKTHIS)							\
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_DBENDIAN, 4, FNLEN, FNNAME, ENDIANOTHER,	\
				ENDIANTHIS);								\
} MBEND

#define FROZEN(CSD)				((CSD)->freeze || FALSE)
#define FROZEN_HARD(CSA)			((CSA)->hdr->freeze && !(CSA)->nl->freeze_online)
#define FROZEN_CHILLED(CSA)			((CSA)->hdr->freeze && (CSA)->nl->freeze_online)
#define FREEZE_LATCH_HELD(CSA)			(process_id == (CSA)->nl->freeze_latch.u.parts.latch_pid)
#define CHILLED_AUTORELEASE_MASK		0x02
#define CHILLED_AUTORELEASE_REPORT_MASK		0x04
#define CHILLED_AUTORELEASE(CSA)		(((CSA)->nl->freeze_online & CHILLED_AUTORELEASE_MASK) || FALSE)
#define CHILLED_AUTORELEASE_REPORTED(CSA)	(((CSA)->nl->freeze_online & CHILLED_AUTORELEASE_REPORT_MASK) || FALSE)
#define DO_CHILLED_AUTORELEASE(CSA, CSD)										\
MBSTART {														\
	GBLREF uint4	process_id;											\
	boolean_t	was_latch;											\
															\
	if (CHILLED_AUTORELEASE(CSA) && !CHILLED_AUTORELEASE_REPORTED(CSA))						\
	{														\
		was_latch = FREEZE_LATCH_HELD(CSA);									\
		if (!was_latch)												\
		{	/* Return value of "grab_latch" does not need to be checked because we pass			\
			 * in GRAB_LATCH_INDEFINITE_WAIT as the timeout.						\
			 */												\
			grab_latch(&(CSA)->nl->freeze_latch, GRAB_LATCH_INDEFINITE_WAIT);				\
		}													\
		if (CHILLED_AUTORELEASE(CSA) && !CHILLED_AUTORELEASE_REPORTED(CSA))					\
		{													\
			(CSD)->freeze = FALSE;										\
			(CSD)->image_count = 0;										\
			(CSA)->nl->freeze_online = CHILLED_AUTORELEASE_MASK | CHILLED_AUTORELEASE_REPORT_MASK;		\
			send_msg_csa(CSA_ARG(CSA) VARLSTCNT(9) ERR_OFRZAUTOREL, 2, REG_LEN_STR((CSA)->region),		\
					ERR_ERRCALL, 3, CALLFROM);							\
		}													\
		if (!was_latch)												\
			rel_latch(&(CSA)->nl->freeze_latch);								\
	}														\
} MBEND

/* This is the structure describing a segment. It is used as a database file header (for MM or BG access methods).
 * The overloaded fields for MM and BG are n_bts, bt_buckets. */

/* ***NOTE*** If the field minor_dbver is updated, please also update gdsdbver.h and db_auto_upgrade.c appropriately
   (see db_auto_upgrade for reasons and description). SE 5/2006
*/
typedef struct sgmnt_data_struct
{
	/************* MOSTLY STATIC DATABASE STATE FIELDS **************************/
	unsigned char	label[GDS_LABEL_SZ];
	int4		blk_size;		/* Block size for the file. Static data defined at db creation time */
	int4		master_map_len;		/* Length of master map */
	int4		bplmap;			/* Blocks per local map (bitmap). static data defined at db creation time */
	int4		start_vbn;		/* starting virtual block number. */
	enum db_acc_method acc_meth;		/* Access method (BG or MM) */
	uint4		max_bts;		/* Maximum number of bt records allowed in file */
	int4		n_bts;			/* number of cache record/blocks */
	int4		bt_buckets;		/* Number of buckets in bt table */
	int4		reserved_bytes;		/* Database blocks will always leave this many bytes unused */
	int4		max_rec_size;		/* maximum record size allowed for this file */
	int4		max_key_size;		/* maximum key size allowed for this file */
	uint4		lock_space_size;	/* Number of bytes to be used for locks (in database for bg) */
	uint4		extension_size;		/* Number of gds data blocks to extend by */
	uint4		def_coll;		/* Default collation type for new globals */
	uint4		def_coll_ver;		/* Default collation type version */
	boolean_t	std_null_coll;		/* 0 -> GT.M null collation,i,e, null subs collate between numeric and string
						 * 1-> standard null collation i.e. null subs collate before numeric and string */
	int		null_subs;
	uint4		free_space;		/* Space in file header not being used */
	mutex_spin_parms_struct	mutex_spin_parms;
	int4		max_update_array_size;	/* maximum size of update array needed for one non-TP set/kill */
	int4		max_non_bm_update_array_size;/* maximum size of update array excepting bitmaps */
	boolean_t	file_corrupt;		/* If set, it shuts the file down.  No process (except DSE) can
						 * successfully map this section after the flag is set to TRUE. Processes
						 * that already have it mapped should produce an error the next time that
						 * they use the file. The flag can only be reset by the DSE utility.
						 */
	enum mdb_ver	minor_dbver;		/* Minor DB version field that is incremented when minor changes to this
						 * file-header or API changes occur. See note at top of sgmnt_data.
						 */
	uint4		jnl_checksum;
	uint4		wcs_phase2_commit_wait_spincnt;	/* # of spin iterations before sleeping while waiting for phase2 commits */
	enum mdb_ver	last_mdb_ver;		/* Minor DB version of the GT.M version that last accessed this database.
						 * Maintained only by GT.M versions V5.3-003 and greater.
						 */
	/* The structure is 128-bytes in size at this point */
	/************* FIELDS SET AT CREATION TIME ********************************/
	char		filler_created[52];	/* Now unused .. was "file_info created" */
	boolean_t	createinprogress;	/* TRUE only if MUPIP CREATE is in progress. FALSE otherwise */
	int4		creation_time4;		/* Lower order 4-bytes of time when the database file was created */
	uint4		reservedDBFlags;	/* Bit mask field containing the reserved DB flags (field copied from gd_region) */

	/************* FIELDS USED BY TN WARN PROCESSING *************************/
	trans_num	max_tn;			/* Hardstop TN for this database */
	trans_num	max_tn_warn;		/* TN for next TN_RESET warning for this database */

	/************* FIELDS SET BY MUPIP BACKUP/REORG *************************/
	trans_num	last_inc_backup;
	trans_num	last_com_backup;
	trans_num	last_rec_backup;
	block_id	last_inc_bkup_last_blk;	/* Last block in the database at time of last incremental backup */
	block_id	last_com_bkup_last_blk;	/* Last block in the database at time of last comprehensive backup */
	block_id	last_rec_bkup_last_blk;	/* Last block in the database at time of last record-ed backup */
	block_id	reorg_restart_block;
	char		filler_256[8];
	/************* FIELDS SET WHEN DB IS OPEN ********************************/
	char		now_running[MAX_REL_NAME];/* for active version stamp */
	uint4		filler_owner_node;	/* 4-byte filler - since owner_node is maintained on VMS only */
	uint4		image_count;		/* for db freezing. Set to "process_id" on Unix and "image_count" on VMS */
	uint4		freeze;			/* for db freezing. Set to "getuid"     on Unix and "process_id"  on VMS */
	int4		kill_in_prog;		/* counter for multi-crit kills that are not done yet */
	int4		abandoned_kills;
	uint4		unused_freeze_online_filler;	/* see field in node_local */
	char		filler_320[4];
	/************* FIELDS USED IN V4 <==> V5 COMPATIBILITY MODE ****************/
	trans_num	tn_upgrd_blks_0;	/* TN when blks_to_upgrd becomes 0.
						 *	 Never set = 0 => we have not achieved this yet,
						 *	Always set = 1 => database was created as V5 (or current version)
						 */
	trans_num	desired_db_format_tn;	/* Database tn when last db format change occurred */
	trans_num	reorg_db_fmt_start_tn;	/* Copy of desired_db_format_tn when MUPIP REORG UPGRADE/DOWNGRADE started */
	block_id	reorg_upgrd_dwngrd_restart_block;	/* Block numbers lesser than this were last upgraded/downgraded by
								 * MUPIP REORG UPGRADE|DOWNGRADE before being interrupted */
	int4		blks_to_upgrd;			/* Blocks not at current block version level */
	int4		blks_to_upgrd_subzero_error;	/* number of times "blks_to_upgrd" potentially became negative */
	enum db_ver	desired_db_format;	/* Output version for database blocks (normally current version) */
	boolean_t	fully_upgraded;		/* Set to TRUE by MUPIP REORG UPGRADE when ALL blocks (including RECYCLED blocks)
						 * have been examined and upgraded (if necessary) and blks_to_upgrd is set to 0;
						 * If set to TRUE, this guarantees all blocks in the database are upgraded.
						 * "blks_to_upgrd" being 0 does not necessarily guarantee the same since the
						 *	counter might have become incorrect (due to presently unknown reasons).
						 * set to FALSE whenever desired_db_format changes or the database is
						 *	updated with V4 format blocks (by MUPIP JOURNAL).
						 */
	boolean_t	db_got_to_v5_once;	/* Set to TRUE by the FIRST MUPIP REORG UPGRADE (since MUPIP UPGRADE was run
						 * to upgrade the file header to V5 format) when it completes successfully.
						 * The FIRST reorg upgrade marks all RECYCLED blocks as FREE. Successive reorg
						 * upgrades keep RECYCLED blocks as they are while still trying to upgrade them.
						 * This is because ONLY the FIRST reorg upgrade could see RECYCLED blocks in V4
						 * format that are too full (lack the additional space needed by the V5 block
						 * header) to be upgraded to V5 format. Once these are marked FREE, all future
						 * block updates happen in V5 format in the database buffers so even if they
						 * are written in V4 format to disk, they are guaranteed to be upgradeable.
						 * This field marks that transition in the db and is never updated thereafter.
						 */
	boolean_t	opened_by_gtmv53;	/* Set to TRUE the first time this database is opened by GT.M V5.3-000 and higher */
	char		filler_384[12];
	/************* FIELDS RELATED TO DB TRANSACTION HISTORY *****************************/
	th_index	trans_hist;		/* transaction history - if moved from 1st filehdr block, change TH_BLOCK */
	char		filler_trans_hist[8];
	/************* FIELDS RELATED TO WRITE CACHE FLUSHING *******************************/
	int4		flush_time[2];
	int4		flush_trigger;
	int4		n_wrt_per_flu;		/* Number of writes per flush call. Overloaded for BG and MM */
	int4		wait_disk_space;        /* seconds to wait for diskspace before giving up on a db block write */
	int4		defer_time;		/* defer write
						 *	 0 => immediate,
						 *	-1 => infinite defer,
						 *	>0 => defer_time * flush_time[0] is actual defer time
						 * default value = 1 => a write-timer every csd->flush_time[0] seconds
						 */
	volatile boolean_t filler_wc_blocked;	/* Now moved to node_local */
	boolean_t	mumps_can_bypass;	/* Allow mumps processes to bypass flushing, access control, and ftok semaphore
						 * in "gds_rundown". This was done to improve shutdown performance.
						 */
	boolean_t	epoch_taper;		/* If TRUE, GT.M tries to reduce dirty buffers as epoch approaches */
	uint4		epoch_taper_time_pct;	/* in the last pct we start tapering for time */
	uint4		epoch_taper_jnl_pct;	/* in the last pct we start tapering for jnl */
	boolean_t	asyncio;		/* If TRUE, GT.M uses async I/O */
	/************* FIELDS Used for update process performance improvement. Some may go away in later releases ********/
	uint4		reserved_for_upd;	/* Percentage (%) of blocks reserved for update process disk read */
	uint4		avg_blks_per_100gbl;	/* Number of blocks read on average for 100 global key read */
	uint4		pre_read_trigger_factor;/* Percentage (%) of blocks  reserved for prereader disk read */
	uint4		writer_trigger_factor;	/* For update process writers flush trigger */
	/************* FIELDS USED ONLY BY UNIX ********************************/
	int4		semid;			/* Since int may not be of fixed size, int4 is used */
	int4		shmid;			/* Since int may not be of fixed size, int4 is used */
	gtm_time8	gt_sem_ctime;		/* time of creation of semaphore */
	gtm_time8	gt_shm_ctime;		/* time of creation of shared memory */
	char		filler_unixonly[40];	/* to ensure this section has 64-byte multiple size */
	/************* ACCOUNTING INFORMATION ********************************/
	int4		filler_n_retries[CDB_MAX_TRIES];/* Now moved to TAB_GVSTATS_REC section */
	int4		filler_n_puts;			/* Now moved to TAB_GVSTATS_REC section */
	int4		filler_n_kills;			/* Now moved to TAB_GVSTATS_REC section */
	int4		filler_n_queries;		/* Now moved to TAB_GVSTATS_REC section */
	int4		filler_n_gets;			/* Now moved to TAB_GVSTATS_REC section */
	int4		filler_n_order;			/* Now moved to TAB_GVSTATS_REC section */
	int4		filler_n_zprevs;		/* Now moved to TAB_GVSTATS_REC section */
	int4		filler_n_data;			/* Now moved to TAB_GVSTATS_REC section */
	uint4		filler_n_puts_duplicate;	/* Now moved to TAB_GVSTATS_REC section */
	uint4		filler_n_tp_updates;		/* Now moved to TAB_GVSTATS_REC section */
	uint4		filler_n_tp_updates_duplicate;	/* Now moved to TAB_GVSTATS_REC section */
	char		filler_accounting_64_align[4];	/* to ensure this section has 64-byte multiple size */
	/************* CCP/RC RELATED FIELDS (CCP STUFF IS NOT USED CURRENTLY BY GT.M) *************/
	int4		staleness[2];		/* timer value */
	int4		ccp_tick_interval[2];	/* quantum to release write mode if no write occurs and others are queued
						 * These three values are all set at creation by mupip_create
						 */
	int4		ccp_quantum_interval[2];/* delta timer for ccp quantum */
	int4		ccp_response_interval[2];/* delta timer for ccp mailbox response */
	boolean_t	ccp_jnl_before;		/* used for clustered to pass if jnl file has before images */
	boolean_t	clustered;		/* FALSE (clustering is currently unsupported) */
	boolean_t	unbacked_cache;		/* FALSE for clustering. TRUE otherwise */

	int4		rc_srv_cnt;		/* Count of RC servers accessing database */
	int4		dsid;			/* DSID value, non-zero when being accessed by RC */
	int4		rc_node;
	char		filler_ccp_rc[8];	/* to ensure this section has 64-byte multiple size */
	/************* REPLICATION RELATED FIELDS ****************/
	seq_num		reg_seqno;		/* the jnl seqno of the last update to this region -- 8-byte aligned */
	seq_num		pre_multisite_resync_seqno;	/* previous resync-seqno field now moved to the replication instance file */
	trans_num	zqgblmod_tn;		/* db tn corresponding to zqgblmod_seqno - used in losttrans handling */
	seq_num		zqgblmod_seqno;		/* minimum resync seqno of ALL -fetchresync rollbacks that happened on a secondary
						 * (that was formerly a root primary) AFTER the most recent
						 * MUPIP REPLIC -LOSTTNCOMPLETE command */
	int4		repl_state;		/* state of replication whether open/closed/was_open */
	boolean_t	multi_site_open;	/* Set to TRUE the first time a process opens the database using
						 * a GT.M version that supports multi-site replication. FALSE until then */
	seq_num		filler_seqno;		/* formerly dualsite_resync_seqno but removed once dual-site support was dropped */
	char		filler_repl[16];	/* to ensure this section has 64-byte multiple size */
	/************* TP RELATED FIELDS ********************/
	int4		filler_n_tp_retries[12];		/* Now moved to TAB_GVSTATS_REC section */
	int4		filler_n_tp_retries_conflicts[12];	/* Now moved to TAB_GVSTATS_REC section */
	int4		tp_cdb_sc_blkmod[8];	/* Notes down the number of times each place got a cdb_sc_blkmod in tp.
						 * Only first 4 array entries are updated now, but space is allocated
						 * for 4 more if needed in the future. */
	/************* JOURNALLING RELATED FIELDS ****************/
	uint4		jnl_alq;
	uint4		jnl_deq;
	int4		jnl_buffer_size;	/* in 512-byte pages */
	boolean_t	jnl_before_image;
	int4		jnl_state;		/* journaling state: same as enum jnl_state_codes in jnl.h */
	uint4		jnl_file_len;		/* journal file name length */
	uint4		autoswitchlimit;	/* limit in disk blocks (max 4GB) when jnl should be auto switched */
	int4		epoch_interval;		/* Time between successive epochs in epoch-seconds */
	uint4		alignsize;		/* alignment size for JRT_ALIGN */
	int4		jnl_sync_io;		/* drives sync I/O ('direct' if applicable) for journals, if set (UNIX) */
	int4		yield_lmt;		/* maximum number of times a process yields to get optimal jnl writes */
	boolean_t	turn_around_point;
	trans_num	jnl_eovtn;		/* last tn for a closed jnl; otherwise epoch tn from the epoch before last */
	char		filler_jnl[8];		/* to ensure this section has 64-byte multiple size */
	/************* INTERRUPTED RECOVERY RELATED FIELDS ****************/
	seq_num		intrpt_recov_resync_seqno;/* resync/fetchresync jnl_seqno of interrupted rollback */
	jnl_tm_t	intrpt_recov_tp_resolve_time;/* since-time for the interrupted recover */
	boolean_t	recov_interrupted;	/* whether a MUPIP JOURNAL RECOVER/ROLLBACK on this db got interrupted */
	int4		intrpt_recov_jnl_state;	/* journaling state at start of interrupted recover/rollback */
	int4		intrpt_recov_repl_state;/* replication state at start of interrupted recover/rollback */
	/************* TRUNCATE RELATED FIELDS ****************/
	uint4		before_trunc_total_blks;	/* Used in recover_truncate to detect interrupted truncate */
	uint4		after_trunc_total_blks;		/* All these fields are used to repair interrupted truncates */
	uint4		before_trunc_free_blocks;
	uint4		filler_trunc;			/* Previously before_trunc_file_size, which is no longer used */
	char		filler_1k[24];
	/************* POTENTIALLY LARGE CHARACTER ARRAYS **************/
	unsigned char	jnl_file_name[JNL_NAME_SIZE];	/* journal file name */
	unsigned char	reorg_restart_key[OLD_MAX_KEY_SZ + 1];	/* 1st key of a leaf block where reorg was done last time.
								 * Note: In mu_reorg we don't save keys longer than OLD_MAX_KEY_SZ
								 */
	char		machine_name[MAX_MCNAMELEN];
	/************* ENCRYPTION-RELATED FIELDS **************/
	/* Prior to the introduction of encryption_hash and, subsequently, other encryption fields, this space was occupied by a
	 * char filler_2k[256]. Now that the encryption fields consume a part of that space, the filler has been reduced in size.
	 */
	char            encryption_hash[GTMCRYPT_RESERVED_HASH_LEN];
	char		encryption_hash2[GTMCRYPT_RESERVED_HASH_LEN];
	boolean_t	non_null_iv;
	block_id	encryption_hash_cutoff;		/* Points to the first block to be encrypted by MUPIP REORG -ENCRYPT with
							 * encryption_hash2. The value of -1 indicates that no (re)encryption is
							 * happening. */
	trans_num	encryption_hash2_start_tn;	/* Indicates the lowest transaction number at which a block is encrypted
							 * with encryption_hash2. */
	char		filler_encrypt[80];
	/***************************************************/
	/* The CLRGVSTATS macro wipes out everything from here through the GVSTATS fields up to intrpt_recov_resync_strm_seqno
	 * starting from the end of the space reserved for the encryption_hash above - DO NOT insert anthing in this range or move
	 * those two end points without appropriately adjusting that macro
	 */
	/************* BG_TRC_REC RELATED FIELDS ***********/
#	define TAB_BG_TRC_REC(A,B)	bg_trc_rec_tn	B##_tn;
#	include "tab_bg_trc_rec.h"
#	undef TAB_BG_TRC_REC
	char		bg_trc_rec_tn_filler  [1200 - (SIZEOF(bg_trc_rec_tn) * n_bg_trc_rec_types)];

#	define TAB_BG_TRC_REC(A,B)	bg_trc_rec_cntr	B##_cntr;
#	include "tab_bg_trc_rec.h"
#	undef TAB_BG_TRC_REC
	char		bg_trc_rec_cntr_filler[600 - (SIZEOF(bg_trc_rec_cntr) * n_bg_trc_rec_types)];

	/************* DB_CSH_ACCT_REC RELATED FIELDS ***********/
#	define	TAB_DB_CSH_ACCT_REC(A,B,C)	db_csh_acct_rec	A;
#	include "tab_db_csh_acct_rec.h"
#	undef TAB_DB_CSH_ACCT_REC
	char		db_csh_acct_rec_filler_4k[248 - (SIZEOF(db_csh_acct_rec) * n_db_csh_acct_rec_types)];

	/************* GVSTATS_REC RELATED FIELDS ***********/
	gvstats_rec_t	gvstats_rec;
	char		gvstats_rec_filler_4k_plus_512[512 - SIZEOF(gvstats_rec_t)];
	char		filler_4k_plus_512[368];	/* Note: this filler array should START at offset 4K+512. So any additions
							 * of new fields should happen at the END of this filler array and
							 * the filler array size correspondingly adjusted.
							 */
	/************* INTERRUPTED RECOVERY RELATED FIELDS continued ****************/
	seq_num		intrpt_recov_resync_strm_seqno[MAX_SUPPL_STRMS];/* resync/fetchresync jnl_seqno of interrupted rollback
									 * corresponding to each non-supplementary stream.
									 */
	/************* DB CREATION AND UPGRADE CERTIFICATION FIELDS ***********/
	enum db_ver	creation_db_ver;		/* Major DB version at time of creation */
	enum mdb_ver	creation_mdb_ver;		/* Minor DB version at time of creation */
	enum db_ver	certified_for_upgrade_to;	/* Version the database is certified for upgrade to */
	int4		filler_5k;
	/************* SECSHR_DB_CLNUP RELATED FIELDS (now moved to node_local) ***********/
	int4		secshr_ops_index_filler;
	int4		secshr_ops_array_filler[255];	/* taking up 1k */
	/********************************************************/
	compswap_time_field next_upgrd_warn;	/* Time when we can send the next upgrade warning to the operator log */
	uint4		is_encrypted;		/* Encryption state of the database as a superimposition of IS_ENCRYPTED and
						 * TO_BE_ENCRYPTED flags. */
	uint4		db_trigger_cycle;	/* incremented every MUPIP TRIGGER command that changes ^#t global contents */
	/************* SUPPLEMENTARY REPLICATION INSTANCE RELATED FIELDS ****************/
	seq_num		strm_reg_seqno[MAX_SUPPL_STRMS];	/* the jnl seqno of the last update to this region for a given
								 * supplementary stream -- 8-byte aligned */
	seq_num		save_strm_reg_seqno[MAX_SUPPL_STRMS];	/* a copy of strm_reg_seqno[] before it gets changed in
								 * "mur_process_intrpt_recov". Used only by journal recovery.
								 * See comment in "mur_get_max_strm_reg_seqno" function for
								 * purpose of this field. Must also be 8-byte aligned.
								 */
	/************* MISCELLANEOUS FIELDS ****************/
	boolean_t	freeze_on_fail;		/* Freeze instance if failure of this database observed */
	boolean_t	span_node_absent;	/* Database does not contain the spanning node */
	boolean_t	maxkeysz_assured;	/* All the keys in the database are less than MAX_KEY_SIZE */
	boolean_t	hasht_upgrade_needed;	/* ^#t global needs to be upgraded from V62000 to post-V62000 format */
	boolean_t	defer_allocate;		/* If FALSE: Use fallocate() preallocate space from the disk */
	boolean_t	filler_ftok_counter_halted;	/* Used only in V6.3-000. Kept as a filler just to be safe */
	boolean_t	filler_access_counter_halted;	/* Used only in V6.3-000. Kept as a filler just to be safe */
	boolean_t	lock_crit_with_db;		/* flag controlling LOCK crit mechanism; see interlock.h */
	uint4		basedb_fname_len;		/* byte length of filename stored in "basedb_fname[]" */
	unsigned char	basedb_fname[256];	/* full path filaneme of corresponding baseDB if this is a statsDB */
	boolean_t	read_only;		/* If TRUE, GT.M uses a process-private mmap instead of IPC */
	char		filler_7k[440];
	/************** YottaDB specific fields *********************/
	uint4		reorg_sleep_nsec;	/* Time a MUPIP REORG sleeps before starting to process a GDS block */
	char		filler_8k[1020];
	/********************************************************/
	/* Master bitmap immediately follows. Tells whether the local bitmaps have any free blocks or not. */
} sgmnt_data;

#ifdef DB64
# ifdef __osf__
#  pragma pointer_size(save)
#  pragma pointer_size(long)
# else
#  error UNSUPPORTED PLATFORM
# endif
#endif

typedef	sgmnt_data		*sgmnt_data_ptr_t;

#ifdef DB64
# ifdef __osf__
#  pragma pointer_size(restore)
# endif
#endif

typedef struct
{
	FILL8DCL(cache_que_heads_ptr_t, cache_state, 1);	/* pointer to beginnings of state queues */
} sgbg_addrs;

typedef struct
{
	int 	filler;
} sgmm_addrs;

#define MAX_NM_LEN	MAX_MIDENT_LEN
#define MIN_RN_LEN	1
#define MAX_RN_LEN	MAX_MIDENT_LEN
#define V4_MAX_RN_LEN	31	/* required for dbcertify.h */
#define MIN_SN_LEN	1
#define MAX_SN_LEN	MAX_MIDENT_LEN
#define STR_SUB_PREFIX  0x0FF
#define SUBSCRIPT_STDCOL_NULL 0x01
#define STR_SUB_ESCAPE  0X01
#define SPANGLOB_SUB_ESCAPE  0X02
#define	STR_SUB_MAXVAL	0xFF
#define SUBSCRIPT_ZERO  0x080
#define SUBSCRIPT_BIAS  ((unsigned char)0x0BE)
#define NEG_MNTSSA_END  0x0FF
#define KEY_DELIMITER   0X00
#ifdef BIGENDIAN
typedef struct
{	unsigned int	two : 4;
	unsigned int	one : 4;
} sub_num;
#else
typedef struct
{	unsigned int	one : 4;
	unsigned int	two : 4;
} sub_num;
#endif
#define MIN_DB_BLOCKS	10	/* this should be maintained in conjunction with the mimimum allocation in GDEINIT.M */
#define MIN_GBUFF_LIMIT	32	/* minimum gbuff limit */
#define REORG_GBUFF_LIMIT "64"	/* default gbuff_limit for REORG */

/* definition for NULL_SUBSCRIPTS */
#define NEVER		0
#define ALWAYS		1
#define ALLOWEXISTING 	2

#define OFFSET(x,y) ((uchar_ptr_t)x - (uchar_ptr_t)y)

#define FC_READ 0
#define FC_WRITE 1
#define FC_OPEN 2
#define FC_CLOSE 3

#define DO_BADDBVER_CHK(REG, TSD)								\
MBSTART {											\
	if (MEMCMP_LIT(TSD->label, GDS_LABEL))							\
	{											\
		if (memcmp(TSD->label, GDS_LABEL, GDS_LABEL_SZ - 3))				\
			rts_error_csa(CSA_ARG(REG2CSA(REG)) VARLSTCNT(4) ERR_DBNOTGDS, 2,	\
					DB_LEN_STR(REG));					\
		else										\
			rts_error_csa(CSA_ARG(REG2CSA(REG)) VARLSTCNT(4) ERR_BADDBVER, 2,	\
					DB_LEN_STR(REG));					\
	}											\
} MBEND

#define DO_DB_HDR_CHECK(REG, TSD)								\
MBSTART {											\
	GBLREF	boolean_t	mupip_jnl_recover;						\
	uint4			gtm_errcode = 0;						\
												\
	if (TSD->createinprogress)								\
		gtm_errcode = ERR_DBCREINCOMP;							\
	if (TSD->file_corrupt && !mupip_jnl_recover)						\
		gtm_errcode = ERR_DBFLCORRP;							\
	if ((dba_mm == TSD->acc_meth) && TSD->blks_to_upgrd)					\
		gtm_errcode = ERR_MMNODYNUPGRD;							\
	if (0 != gtm_errcode)									\
	{											\
		if (IS_DSE_IMAGE)								\
		{										\
			gtm_errcode = MAKE_MSG_WARNING(gtm_errcode);				\
			gtm_putmsg_csa(CSA_ARG(REG2CSA(REG)) VARLSTCNT(4) gtm_errcode, 2,	\
					DB_LEN_STR(REG));					\
		} else										\
			rts_error_csa(CSA_ARG(REG2CSA(REG)) VARLSTCNT(4) gtm_errcode, 2,	\
					DB_LEN_STR(REG));					\
	}											\
} MBEND

#define REG2CSA(REG)	(((REG) && (REG)->dyn.addr && (REG)->dyn.addr->file_cntl) ? (&FILE_INFO(REG)->s_addrs) : NULL)
#define JCTL2CSA(JCTL)	(((JCTL) && (JCTL->reg_ctl)) ? (JCTL->reg_ctl->csa) : NULL)

/* A structure to store the encryption settings when a reorg_encrypt_cycle change is detected in shared memory in the middle of a
 * transaction. We updated the private copy of encryption settings and (re)initialize handles, if needed, based on this information
 * before restarting the transaction. Note that this structure should be populated at a safe time, such as while holding crit or
 * having otherwise ensured that MUPIP REORG -ENCRYPT cannot cross the boundary of another reorg_encrypt_cycle (such as in
 * dsk_read.c and wcs_wtstart.c); however, read access does not require crit (such as in jnl_format.c).
 */
typedef struct
{
	uint4		reorg_encrypt_cycle;
	uint4		is_encrypted;
	boolean_t	non_null_iv;
	block_id	encryption_hash_cutoff;
	trans_num	encryption_hash2_start_tn;
	char            encryption_hash[GTMCRYPT_HASH_LEN];
	char		encryption_hash2[GTMCRYPT_HASH_LEN];
	boolean_t	issued_db_init_crypt_warning;	/* Indicates whether we issued a warning-severity encryption-setup-related
							 * message in db_init for a non-mumps process */
	uint4		filler;
} enc_info_t;

/* Macro to copy the encryption information into an enc_info_t structure. */
#define COPY_ENC_INFO_INT(SRC, DST, REORG_ENCRYPT_CYCLE)						\
MBSTART {												\
	(DST)->reorg_encrypt_cycle = REORG_ENCRYPT_CYCLE;						\
	(DST)->is_encrypted = (SRC)->is_encrypted;							\
	(DST)->non_null_iv = (SRC)->non_null_iv;							\
	(DST)->encryption_hash_cutoff = (SRC)->encryption_hash_cutoff;					\
	(DST)->encryption_hash2_start_tn = (SRC)->encryption_hash2_start_tn;				\
	memcpy((DST)->encryption_hash, (SRC)->encryption_hash, GTMCRYPT_HASH_LEN);			\
	memcpy((DST)->encryption_hash2, (SRC)->encryption_hash2, GTMCRYPT_HASH_LEN);			\
	DEBUG_ONLY((DST)->filler = 0;)									\
} MBEND													\

/* Macro to copy the encryption information into an enc_info_t structure. */
#define COPY_ENC_INFO(SRC, DST, REORG_ENCRYPT_CYCLE)							\
MBSTART {												\
	DEBUG_ONLY(enc_info_t	before);								\
													\
	DEBUG_ONLY(COPY_ENC_INFO_INT(SRC, &before, REORG_ENCRYPT_CYCLE));				\
	/* This is to have the following memcmp succeed; normally, the issued_db_init_crypt_warning 	\
	 * field is never updated once set.								\
	 */												\
	DEBUG_ONLY(before.issued_db_init_crypt_warning = (DST)->issued_db_init_crypt_warning);		\
	COPY_ENC_INFO_INT(SRC, DST, REORG_ENCRYPT_CYCLE);						\
	/* This macro is not necessarily invoked while holding crit, yet none of its usages should be	\
	 * subject to concurrent database file header changes of encrypted settings, so assert that.	\
	 * The only exception is "encryption_hash_cutoff" which could be concurrently changed (by a	\
	 * MUPIP REORG ENCRYPT) but is not critical encryption information so skip that part.		\
	 */												\
	DEBUG_ONLY(before.encryption_hash_cutoff = DST->encryption_hash_cutoff;)			\
	DEBUG_ONLY(assert(!memcmp(&before, DST, SIZEOF(enc_info_t))));					\
} MBEND													\

#define	INITIALIZE_CSA_ENCR_PTR(CSA, CSD, UDI, DO_CRYPT_INIT, CRYPT_WARNING, DEFER_COPY)				\
MBSTART {														\
	GBLREF bool	in_mupip_freeze;										\
															\
	if (DO_CRYPT_INIT)												\
	{														\
		CSA->encr_ptr = (enc_info_t *)malloc(SIZEOF(enc_info_t));						\
		/* It should be safe to copy encryption key information from CSD to CSA because only a concurrent	\
		 * REORG -ENCRYPT may be changing these fields, and it takes the ftok access control semaphore on	\
		 * a live (non-standalone) database before changing things. But the caller of this macro is expected to	\
		 * hold either the ftok access control semaphore (udi->grabbed_fotk_sem) OR the database access control	\
		 * semaphore (udi->grabbed_access_sem) in case of standalone access. This ensures a safe copy. The only	\
		 * exception is if caller is "db_init" and we are DSE, LKE, or MUPIP FREEZE, as they can bypass getting	\
		 * the ftok semaphore, but in those cases we do not rely much on the encryption settings and in the	\
		 * places where we do rely, we expect the users know what they are doing with these admin tools.	\
		 * Assert accordingly.											\
		 */													\
		assert(UDI->grabbed_ftok_sem || UDI->grabbed_access_sem || IS_DSE_IMAGE || IS_LKE_IMAGE			\
			|| (IS_MUPIP_IMAGE && in_mupip_freeze));							\
		if (!(DEFER_COPY))											\
		{													\
			COPY_ENC_INFO(CSD, CSA->encr_ptr, CSA->nl->reorg_encrypt_cycle);				\
			CSA->encr_ptr->issued_db_init_crypt_warning = CRYPT_WARNING;					\
		} else													\
		{	/* Defer the copy until needed later, as detected by a mismatch in reorg_encrypt_cycle.	*/	\
			memset(CSA->encr_ptr, 0, SIZEOF(enc_info_t));							\
			CSA->encr_ptr->reorg_encrypt_cycle = -1;							\
		}													\
	} else														\
		CSA->encr_ptr = NULL;											\
} MBEND

/* Encryption key reinitialization cannot be safely done if we are in the middle of a TP transaction that has already
 * done some updates to a journaled database (old keys would have been used for prior calls to "jnl_format" in this
 * transaction) as otherwise we would be a mix of journal records encrypted using old and new keys in the same TP
 * transaction. Just to be safe, we do the same for read-only TP transaction ("dollar_tlevel" global variable covers
 * both these cases) as well as a non-TP transaction that is read_write ("update_trans" global variable covers this case).
 * In all these cases, we know the caller is capable of restarting the transaction which will sync up the cycles at a safe
 * point (start of the retry).
 *
 * If both these global variables are zero, it is possible this is a non-TP transaction that is read-only OR a non-transaction.
 * In the latter case, it is not just safe but essential to sync new keys since callers might be relying on this.
 * In the former case, it is thankfully safe to sync so we do the sync if both these variables are zero.
 *
 * If it is unsafe to sync keys, the caller of this macro has to cause a restart of the ongoing transaction (caller "t_qread")
 * OR skip doing encrypt/decrypt operations (caller "wcs_wtstart")
 */
#define IS_NOT_SAFE_TO_SYNC_NEW_KEYS(DOLLAR_TLEVEL, UPDATE_TRANS)	(DOLLAR_TLEVEL || UPDATE_TRANS)

#define SIGNAL_REORG_ENCRYPT_RESTART(REORG_ENCRYPT_IN_PROG, REORG_ENCRYPT_CSA, CNL, CSA, CSD, STATUS, PID)	\
MBSTART {													\
	assert(!REORG_ENCRYPT_IN_PROG);										\
	DBG_RECORD_BLOCK_RETRY(CSD, CSA, CNL, PID);								\
	COPY_ENC_INFO(CSD, (CSA)->encr_ptr, (CNL)->reorg_encrypt_cycle);					\
	assert(NULL == REORG_ENCRYPT_CSA);									\
	REORG_ENCRYPT_CSA = CSA;										\
	STATUS = cdb_sc_reorg_encrypt;										\
} MBEND

typedef struct	file_control_struct
{
	sm_uc_ptr_t	op_buff;
	gtm_int64_t	op_pos;
	int		op_len;
	void		*file_info;   /* Pointer for OS specific struct */
	char		op;
} file_control;

typedef struct	header_struct_struct
{
	char		label[12];
	unsigned	filesize;	/* size of file excluding GDE info */

	/* removed unused file_log struct */
} header_struct;

typedef struct gdr_name_struct
{
	mstr			name;
	mstr			exp_name;
	struct gdr_name_struct	*link;
	struct gd_addr_struct	*gd_ptr;
} gdr_name;

typedef struct gd_inst_info_struct
{
	char		instfilename[MAX_FN_LEN + 1];	/* + 1 for null */
} gd_inst_info;

/* Structure that holds all information that is relevant only at runtime.
 * The gld has space allotted for a pointer in the file and we use that to point to this structure at runtime.
 * This lets us freely add to this structure without affecting the gld format and yet maintain gld-specific information
 * (e.g. whether a jnlpool for this gld has been inited or not, info on multiplexing thread for linux AIO etc.).
 */
typedef struct gd_runtime
{
	struct gd_info			*thread_gdi;	/* has information on the multiplexing thread - only used on linux AIO */
	struct jnlpool_addrs_struct	*jnlpool;	/* pointer to jnlpool corresponding to this gld if one exists */
} gd_runtime_t;

typedef struct	gd_addr_struct
{
	struct gd_region_struct		*local_locks;
	int4				max_rec_size;
	uint4				n_maps;
	uint4				n_regions;
	uint4				n_segments;
	uint4				n_gblnames;	/* could be 0 if no global has any collation characteristics defined */
	uint4				var_maps_len;	/* length (in bytes) of variable MAPS sections in .gld file */
	struct gd_binding_struct	*maps;
	struct gd_region_struct		*regions;
	struct gd_segment_struct	*segments;
	struct gd_gblname_struct	*gblnames;
	struct gd_inst_info_struct	*instinfo;
	struct gd_addr_struct		*link;
	struct hash_table_mname_struct  *tab_ptr;
	gd_id				*id;
	UINTPTR_T			end;
	uint4				has_span_gbls;	/* has at least one global which spans multiple regions */
	bool				ygs_map_entry_changed;	/* used by "gvcst_init_statsDB" */
	bool				is_dummy_gbldir;	/* TRUE if this structure is created by "create_dummy_gbldir".
								 * FALSE if this structure is created by "gd_load".
								 */
#ifdef GTM64
	char				filler[2];	/* filler to store runtime structures without changing gdeget/gdeput.m */
#else
	char				filler[6];
#endif
	gd_runtime_t			*gd_runtime;
} gd_addr;

typedef gd_addr *(*gd_addr_fn_ptr)();

typedef struct	gd_segment_struct
{
	unsigned short		sname_len;
	unsigned char		sname[MAX_SN_LEN + 1];
	unsigned short		fname_len;
	unsigned char		fname[MAX_FN_LEN + 1];
	unsigned short		blk_size;
	unsigned short		ext_blk_count;
	uint4			allocation;
	struct CLB		*cm_blk;
	unsigned char		defext[4];
	char			defer_time; 	/* Was passed in cs_addrs */
	unsigned char		file_type;
	unsigned char		buckets;	/* Was passed in FAB */
	unsigned char		windows;	/* Was passed in FAB */
	uint4			lock_space;
	uint4			global_buffers;	/* Was passed in FAB */
	uint4			reserved_bytes;	/* number of bytes to be left in every database block */
	uint4			mutex_slots;	/* copied over to NUM_CRIT_ENTRY(CSD) */
	boolean_t		defer_allocate; /* If FALSE: Use fallocate() preallocate space from the disk */
	enum db_acc_method	acc_meth;
	file_control		*file_cntl;
	struct gd_region_struct	*repl_list;
	uint4			is_encrypted;
	boolean_t		asyncio;	/* copied over to csd->asyncio at db creation time */
	boolean_t		read_only;
	char			filler[12];	/* filler to store runtime structures without changing gdeget/gdeput.m */
} gd_segment;

typedef union
{
	int4             offset;  /* relative offset to segment  */
	gd_segment       *addr;   /* absolute address of segment */
} gd_seg_addr;

typedef struct	gd_region_struct
{
	unsigned short		rname_len;
	unsigned char		rname[MAX_RN_LEN + 1];
	unsigned short		max_key_size;
	uint4			max_rec_size;
	gd_seg_addr		dyn;
	gd_seg_addr		stat;
	bool			open;
	bool			lock_write;	/* Field is not currently used by GT.M */
	char            	null_subs;	/* 0 ->NEVER(previous NO), 1->ALWAYS(previous YES), 2->ALLOWEXISTING
						* i.e. will allow read null subs but prohibit set */
	unsigned char 		jnl_state;
	/* deleted gbl_lk_root and lcl_lk_root, obsolete fields */
	uint4			jnl_alq;
	uint4			jnl_deq;
	uint4			jnl_autoswitchlimit;
	uint4			jnl_alignsize;		/* not used, reserved */
	int4			jnl_epoch_interval;	/* not used, reserved */
	int4			jnl_sync_io;		/* not used, reserved */
	int4			jnl_yield_lmt;		/* not used, reserved */
	unsigned short		jnl_buffer_size;
	bool			jnl_before_image;
	bool			opening;
	bool			read_only;
	bool			was_open;
	unsigned char		cmx_regnum;
	unsigned char		def_coll;
	bool			std_null_coll;	/* 0 -> GT.M null collation,i,e, null subs collate between numeric and string
						 * 1-> standard null collation i.e. null subs collate before numeric and string
						 */
	bool			freeze_on_fail;
	bool			mumps_can_bypass; /* Allow mumps processes to bypass flushing, access control, and ftok semaphore
						   * in "gds_rundown". This was done to improve shutdown performance.
						   */
	unsigned char		jnl_file_len;
	unsigned char		jnl_file_name[JNL_NAME_SIZE];
	int4			node;
	int4			sec_size;
	uint4			is_spanned;		/* This is one of the regions that some spanning global maps to */
	uint4			statsDB_reg_index;	/* If this is a base region, this is the region index of the
							 *	corresponding statsDB region.
							 * If this is a statsDB region, this is the region index of the
							 *	corresponding base region.
							 */
	bool			epoch_taper;
	bool			reservedDBFlags; 	/* Flags for reservedDB types and/or features */
	bool			lock_crit_with_db;	/* controls whether LOCK crit is separate (0) or shared with DB (1) */
	/* All fields before this point are relied upon by GDE. All fields after this point are relied upon only by
	 * the runtime logic (i.e. it is one big filler/padding area as far as GDE is concerned).
	 */
	bool			statsDB_setup_started;
	gd_addr			*owning_gd;
	bool			statsDB_setup_completed;
	char			filler[39];	/* filler to store runtime structures without changing gdeget/gdeput.m */
} gd_region;

typedef struct	sgmnt_addrs_struct
{
	sgmnt_data_ptr_t			hdr;
	sm_uc_ptr_t				bmm;
	sm_uc_ptr_t				wc;
	bt_rec_ptr_t				bt_header;
	bt_rec_ptr_t				bt_base;
	th_rec_ptr_t				th_base;
	th_index_ptr_t				ti;
	node_local_ptr_t			nl;
	mutex_struct_ptr_t			critical;
	struct shmpool_buff_hdr_struct		*shmpool_buffer;	/* 1MB chunk of shared memory that we micro manage */
	sm_uc_ptr_t				db_addrs[2];
	sm_uc_ptr_t				lock_addrs[2];
	struct gv_namehead_struct		*dir_tree;
#	ifdef GTM_TRIGGER
	struct gv_namehead_struct		*hasht_tree;
#	endif
	struct sgmnt_addrs_struct		*next_fenced;		/* NULL if db has journaling turned off (or disabled)
									 * Otherwise (db has journaling turned on), it is
									 *  NULL if this db was not updated in this TP/ZTP
									 *  non-NULL if this db was updated in this TP/ZTP
									 * The non-NULL value points to the next csa that
									 * has a non-NULL next_fenced value i.e. a linked list
									 * of csas. The end of the list is JNL_FENCE_LIST_END
									 * (cannot use NULL due to special meaning described
									 * above and hence using a macro which evaluates to -1).
									 */
	struct jnl_private_control_struct	*jnl;
	struct sgm_info_struct			*sgm_info_ptr;
	gd_region				*region;		/* the region corresponding to this csa */
	struct hash_table_mname_struct		*gvt_hashtab;		/* NON-NULL only if regcnt > 1;
								 	 * Maintains all gv_targets mapped to this db file */
	void					*miscptr;	/* pointer to rctl for this region (if jgbl.forw_phase_recovery)
								 * pointer to gvt_hashtab for this region (if DSE_IMAGE)
								 * pointer to repl_rctl for this region (if source server)
								 * NULL in all other cases.
								 */
	struct sgmnt_addrs_struct		*next_csa; /* points to csa of NEXT database that has been opened by this process */
	gtmcrypt_key_t				encr_key_handle;
	gtmcrypt_key_t				encr_key_handle2;
	enc_info_t				*encr_ptr;	/* Copy of encryption info from the database file header */
	struct snapshot_context_struct 		*ss_ctx;
	union
	{
		sgmm_addrs	mm;
		sgbg_addrs	bg;
		/* May add new pointers here for other methods or change to void ptr */
	} acc_meth;
	gvstats_rec_t		*gvstats_rec_p;	/* Pointer to either the stats in this structure or the stats in a shared
						 * global gvstats_rec_t. All access to process stats should be through this ptr.
						 */
	gvstats_rec_t		gvstats_rec;
	trans_num		dbsync_timer_tn;/* copy of csa->ti->curr_tn when csa->dbsync_timer became TRUE.
						 * used to check if any updates happened in between when we flushed all
						 * dirty buffers to disk and when the idle flush timer (5 seconds) popped.
						 */
	/* 8-byte aligned at this point on all platforms (32-bit, 64-bit or Tru64 which is a mix of 32-bit and 64-bit pointers) */
	cache_rec_ptr_t	our_midnite;		/* anchor if we are using a gbuff_limit */
	size_t		fullblockwrite_len;	/* Length of a full block write */
	sm_off_t 	our_lru_cache_rec_off;	/* last used cache pointer for when we are using a gbuff_limit */
	uint4		total_blks;		/* Last we knew, file was this big. Used to signal MM processing file was
						 * extended and needs to be remapped. In V55000 was used with BG to detect
						 * file truncates. It is no longer used for that purpose: it was not necessary
						 * in the first place because bitmap block validations in t_end/tp_tend prevent
						 * updates from trying to commit past the end of the file.
						 * See mu_truncate.c for more details.
						 */
	uint4		prev_free_blks;
	/* The following uint4's are treated as bools but must be 4 bytes to avoid interaction between
	   bools in interrupted routines and possibly lost data */
	volatile uint4	timer;                  /* This process has a timer for this region */
	volatile uint4	in_wtstart;		/* flag we are busy writing */
	volatile uint4	now_crit;		/* This process has the critical write lock */
	volatile uint4	wbuf_dqd;		/* A write buffer has been dequeued - signals that
						   extra cleanup required if die while on */
	uint4		stale_defer;		/* Stale processing deferred this region */
	boolean_t	freeze;
	volatile boolean_t	dbsync_timer;	/* whether a timer to sync the filehdr (and write epoch) is active */
	block_id	reorg_last_dest;	/* last destinition block used for swap */
	boolean_t	jnl_before_image;
	boolean_t	read_write;
	boolean_t	orig_read_write;	/* copy of "csa->read_write" at dbfilopn time (needed at gds_rundown time
						 * to know real permissions in case csa->read_write gets reset (e.g. for statsdb)
						 */
	boolean_t	persistent_freeze;	/* if true secshr_db_clnup() won't unfreeze this region */
	/* The following 3 fields are in cs_addrs instead of in the file-header since they are a function
	 * of the journal-record sizes that can change with journal-version-numbers (for the same database).
	 */
	int4		pblk_align_jrecsize;	/* maximum size of a PBLK record with corresponding ALIGN record */
	int4		min_total_tpjnl_rec_size;	/* minimum journal space requirement for a TP transaction */
	int4		min_total_nontpjnl_rec_size;	/* minimum journal space requirement for a non-TP transaction */
	int4		jnl_state;		/* journaling state: it can be 0, 1 or 2 (same as enum jnl_state_codes in jnl.h) */
	int4		repl_state;		/* state of replication whether open/closed/was_open */
	uint4		crit_check_cycle;	/* Used to mark which regions in a transaction legiticamtely have crit */
	int4		backup_in_prog;		/* true if online backup in progress for this region (used in op_tcommit/tp_tend) */
	boolean_t	snapshot_in_prog;	/* true if snapshots are in progress for this region */
	int4		ref_cnt;		/* count of number of times csa->nl->ref_cnt was incremented by this process */
	int4		fid_index;		/* index for region ordering based on unique_id */
	int4		do_fullblockwrites;	/* This region enabled for full block writes */
	int4		regnum;			/* Region number (region open counter) used by journaling so all tokens
						   have a unique prefix per region (and all regions have same prefix)
						*/
	int4		n_pre_read_trigger;	/* For update process to keep track of progress and when to trigger pre-read */
	uint4		jnlpool_validate_check;	/* See the comment above VALIDATE_INITIALIZED_JNLPOOL for details on this field */
	int4		regcnt;			/* # of regions that have this as their csa */
	boolean_t	t_commit_crit;		/* FALSE by default. Non-zero value if in the middle of database commit.
						 * This assumes the following additional values.
						 *   = T_COMMIT_CRIT_PHASE0 if commit started and jnl records are being written
						 *   = T_COMMIT_CRIT_PHASE1 just before calling "bg_update_phase1/mm_update"
						 *   = T_COMMIT_CRIT_PHASE2 just before calling "bg_update_phase2"
						 */
	boolean_t	wcs_pidcnt_incremented;	/* set to TRUE if we incremented cnl->wcs_phase2_commit_pidcnt.
						 * used by secshr_db_clnup to decrement the shared counter. */
	boolean_t	incr_db_trigger_cycle;	/* set to FALSE by default. set to TRUE if trigger state change (in ^#t) occurs for
						 * any global in this database which means an increment to csa->db_trigger_cycle and
						 * csd->db_trigger_cycle. Currently used by MUPIP TRIGGER/$ZTRIGGER(), MUPIP RECOVER
						 * and UPDATE PROCESS
						 */
	uint4		db_trigger_cycle;	/* mirror of csd->db_trigger_cycle; used to detect concurrent ^#t global changes */
	uint4		db_dztrigger_cycle;	/* incremented on every $ZTRIGGER() operation. Due to the presence of $ZTRIGGER()
						 * and ZTRIGGER command the 'd' prefix for ztrigger in db_dztrigger_cycle is used
						 * to denote the '$' in $ZTRIGGER() */
	boolean_t	hold_onto_crit;		/* TRUE currently for dse if a CRIT -SEIZE has been done on this region.
						 * Set to FALSE by a DSE CRIT -RELEASE done on this region. Will also be TRUE in
						 * case of ONLINE ROLLBACK. Any code that can be invoked by both DSE and ROLLBACK
						 * should use csa->hold_onto_crit.
						 */
	boolean_t	dse_crit_seize_done;	/* TRUE if DSE does a CRIT -SEIZE for this region. Set to FALSE when CRIT -RELEASE
						 * or CRIT -REMOVE is done. Other than the -SEIZE and -RELEASE window, if any other
						 * DSE module sets csa->hold_onto_crit to TRUE (like dse_b_dmp) but encounters a
						 * runtime error before getting a chance to do a rel_crit, preemptive_db_clnup
						 * should know to release crit even if hold_onto_crit is set to TRUE and so will
						 * rely on this variable
						 */
	int4		gbuff_limit;		/* desired limit on global buffers; see db_csh_getn, op_view and op_fnview */
	uint4		root_search_cycle;	/* local copy of cnl->root_search_cycle */
	uint4		onln_rlbk_cycle;	/* local copy of cnl->onln_rlbk_cycle */
	uint4		db_onln_rlbkd_cycle;	/* local copy of cnl->db_onln_rlbkd_cycle */
	uint4		reservedDBFlags;	/* Bit mask field containing the reserved DB flags (field copied from gd_region) */
	boolean_t	read_only_fs;		/* TRUE if the region is read_only and the header was not updated due to EROFS */
	boolean_t	crit_probe;		/* flag for indicating the process is doing a crit probe on this region */
	boolean_t	canceled_flush_timer;	/* a flush timer was canceled even though dirty buffers might still exist */
	probecrit_rec_t	probecrit_rec;		/* fields defined in tab_probecrit_rec.h and initialized in probecrit_rec.h */
	boolean_t	lock_crit_with_db;	/* copy of sgmnt_data field controlling LOCK crit mechanizm - see interlock.h */
	boolean_t	needs_post_freeze_online_clean;	/* Perform cleanup of online freeze */
	boolean_t	needs_post_freeze_flushsync;	/* Perform post-freeze flush/sync */
	block_id	tp_hint;		/* last tp (allocation) hint for this process in this region */
	boolean_t	statsDB_setup_completed;	/* TRUE if ^%YGS node has been added to this statsDB file.
							 * Is a copy of reg->statsDB_setup_completed but is present in "csa"
							 * too to handle was_open regions.
							 */
	gd_inst_info	*gd_instinfo;		/* global directory not ydb_repl_instance */
	gd_addr		*gd_ptr;		/* global directory for region */
	struct jnlpool_addrs_struct	*jnlpool;	/* NULL until put, kill, or other function requiring jnlpool */
} sgmnt_addrs;

typedef struct gd_binding_struct
{
	union
	{
		char		*addr;
		uint4		offset;
	} gvkey;			/* Any input key GREATER THAN OR EQUAL TO "gvkey" lies OUTSIDE this map */
	union
	{
		gd_region	*addr;
		uint4		offset;
	} reg;
	uint4		gvname_len;	/* the unsubscripted global name length */
	uint4		gvkey_len;	/* the subscripted global name length excluding the second terminating null byte.
					 *	Is equal to "gvname_len" + 1 if there are no subscripts.
					 */
} gd_binding;

typedef struct gd_gblname_struct
{
        unsigned char   gblname[MAX_NM_LEN + 1];
        uint4           act;    /* alternative collation sequence # */
        uint4           ver;	/* version of collation library used at gld creation time */
} gd_gblname;

#define	INVALID_STATSDB_REG_INDEX	(MAXUINT4)	/* this has to be maintained in parallel with TWO(32)-1 in gdeput.m  */

/* Define macros that provide a connection between statsdb initialization code in GDE & GT.M */
#define	STATSDB_BLK_SIZE	1024	/* the BLK_SIZE computed by GDE for every statsdb region */
#define	STATSDB_ALLOCATION	2050	/* the ALLOCATION computed by GDE for every statsdb region */
#define	STATSDB_EXTENSION	2050	/* the EXTENSION computed by GDE for every statsdb region */
#define	STATSDB_MAX_KEY_SIZE	64	/* the MAX_KEY_SIZE computed by GDE for every statsdb region */
#define	STATSDB_MAX_REC_SIZE	(STATSDB_BLK_SIZE - SIZEOF(blk_hdr)) /* the MAX_REC_SIZE computed by GDE for every statsdb region */

/* The following struct is built into a separate list for each transaction because it is not thrown away if a transaction restarts.
 * The list keeps growing so we can lock down all the necessary regions in the correct order in case one attempt doesn't get very
 * far while later attempts get further. Items will be put on the list sorted in unique_id order so that they will always be
 * grab-crit'd in the same order thus avoiding deadlocks.
 * The structure and the insert_region function that maintains it are also abused/used by mupip and by view_arg_convert on behalf
 * of op_view - the secondary adopters use a different anchor from the tp_reg_list GBLREF and a hence different list.
 */

/* The structure backup_region_list defined in mupipbckup.h needs to have its first four fields
   identical to the first three fields in this structure */
typedef struct tp_region_struct
{
	struct	tp_region_struct *fPtr;		/* Next in list */
	gd_region	*reg;			/* Region pointer. Note that it is not necessarily unique since multiple
						 * regions could point to the same physical file (with all but one of them
						 * having reg->was_open set to TRUE.and hence have the same tp_region structure.
						 */
	gd_id		file_id;
	int4		fid_index;		/* copy of csa->fid_index for this region */
} tp_region;

typedef struct
{
	unsigned short	offset;
	unsigned short	match;
} srch_rec_status;

typedef struct srch_blk_status_struct
{
	cache_rec_ptr_t	cr;
	sm_uc_ptr_t     buffaddr;
	block_id	blk_num;
	trans_num	tn;
	srch_rec_status	prev_rec,
			curr_rec;
	int4		cycle;
	int4		level;
	struct cw_set_element_struct	*cse;
	struct srch_blk_status_struct	*first_tp_srch_status;	/* In TP, this points to an entry in the si->first_tp_hist array
								 * that contains the srch_blk_status structure of this block the
								 * first time it was referenced in this TP transaction. So basically
								 * gvt->hist contains pointers to the first_tp_hist array. At
								 * tp_clean_up time, the first_tp_hist array is cleared but all
								 * pointers to it are not cleaned up then. That instead happens
								 * when the gvt->clue gets used first in the next TP transaction,
								 * at which point we are guaranteed local_tn is much higher than
								 * gvt->read_local_tn which is an indication to complete this
								 * deferred cleanup.
								 * In non-TP, this field is maintained in most but not all places
								 * (e.g. if gvcst_search uses the clue and does not go to t_qread,
								 * this field is not maintained) so do not rely on this in non-TP.
								 */
	struct gv_namehead_struct	*blk_target;
} srch_blk_status;

/* Defines for "cycle" member in srch_blk_status.
 * For histories pointing to shared-memory buffers,
 *	"cycle" will be CYCLE_SHRD_COPY in MM and some positive number in BG.
 * For histories pointing to privately-built blocks,
 *	"cycle" will be CYCLE_PVT_COPY for both BG and MM.
 */
#define		CYCLE_PVT_COPY		-1
#define		CYCLE_SHRD_COPY		-2

typedef struct
{
	int4		depth;
	int4		filler;
	srch_blk_status	h[MAX_BT_DEPTH + 1];
} srch_hist;

#define SUPER_HIST_SIZE		2 * MAX_BT_DEPTH + 2	/* can be increased in the future to accommodate more than 2 histories */

/* Currently used only by MUPIP REORG in order to pass 3 search histories to t_end. */
typedef struct
{
	int4		depth;				/* t_end's validations don't depend on this field */
	int4		filler;
	srch_blk_status h[SUPER_HIST_SIZE];
} super_srch_hist;

#define MERGE_SUPER_HIST(SUPER_HIST, HIST1, HIST2)								\
MBSTART {	/* Possible enhancement: do memcpy instead of loop */						\
	srch_hist 	*hist;											\
	srch_blk_status	*t0, *t1;										\
														\
	(SUPER_HIST)->depth = 1 + (HIST1)->depth + (HIST2)->depth;						\
	assert(SUPER_HIST_SIZE > (SUPER_HIST)->depth);								\
	t0 = (SUPER_HIST)->h;											\
	for (hist = (HIST1);  (NULL != hist);  hist = (hist == (HIST1)) ? (HIST2) : NULL)			\
	{													\
		for (t1 = hist->h;  t1->blk_num;  t1++)								\
		{												\
			*t0 = *t1;										\
			t0++;											\
		}												\
	}													\
	t0->blk_num = 0;											\
} MBEND

typedef struct	gv_key_struct
{
	unsigned short	top;		/* Offset to top of buffer allocated for the key */
	unsigned short	end;		/* End of the current key. Offset to the second null */
	unsigned short	prev;		/* Offset to the start of the previous subscript.
					 * This is used for global nakeds.
					 */
	unsigned char	base[1];	/* Base of the key */
} gv_key;

/* Define macro that will return the size of an array of "gv_key" structures. This is used to allocate temporary key structures
 * to save/restore gv_currkey (for example). Defining the array as a "gv_key" array instead of a "char" array ensures we
 * get the alignment we want (e.g. gv_key->end can be dereferenced without concerns for alignment issues).
 */
#define	DBKEYALLOC(KSIZE)	(1 + DIVIDE_ROUND_UP(DBKEYSIZE(KSIZE), SIZEOF(gv_key)))	/* 1 is for "gv_key" structure at start */

/* The direction that the newly added record went after a block split at a given level */
enum split_dir
{
	NEWREC_DIR_FORCED,	/* direction forced due to one of the sides being too-full i.e. no choice */
	NEWREC_DIR_LEFT,	/* new record went into the end of the left block after the split */
	NEWREC_DIR_RIGHT,	/* new record went into the beginning of the right block after the split */
};

/* Any change to this structure should also have a corresponding [re]initialization in mupip_recover.c
 * in the code where we play the records in the forward phase i.e. go through each of the jnl_files
 * and within if (mur_options.update), initialize necessary fields of gv_target before proceeding with mur_forward().
 */
typedef struct	gv_namehead_struct
{
	gv_key		*first_rec, *last_rec;		/* Boundary recs of clue's data block */
	struct gv_namehead_struct *next_gvnh;		/* Used to chain gv_target's together */
	struct gv_namehead_struct *prev_gvnh;		/* Used to chain gv_target's together */
	struct gv_namehead_struct *next_tp_gvnh;	/* Used to chain gv_targets participating in THIS TP transaction */
	sgmnt_addrs	*gd_csa;			/* Pointer to Segment corresponding to this key */
	srch_hist	*alt_hist;			/* alternate history. initialized once per gv_target */
	struct collseq_struct	*collseq;		/* pointer to a linked list of user supplied routine addresses
							   for internationalization */
	trans_num	read_local_tn;			/* local_tn of last reference for this global */
	GTMTRIG_ONLY(trans_num trig_local_tn;)		/* local_tn of last trigger driven for this global */
	GTMTRIG_ONLY(trans_num trig_read_tn;)		/* local_tn when triggers for this global (^#t records) were read from db */
	gv_key		*prev_key;			/* Points to fully expanded previous key. Used by $zprevious.
							 * Valid only if clue->end is non-zero.
							 */
	boolean_t	noisolation;     		/* whether isolation is turned on or off for this global */
	block_id	root;				/* Root of global variable tree */
	mname_entry	gvname;				/* the name of the global */
	srch_hist	hist;				/* block history array */
	int4		regcnt;				/* number of global directories whose hash-tables point to this gv_target.
							 * 1 by default. > 1 if the same name in TWO DIFFERENT global directories
							 * maps to the same physical file (i.e. two regions in different global
							 * directories have the same physical file).
							 */
	uint4		nct;				/* numerical collation type for internalization */
	uint4		act;				/* alternative collation type for internalization */
	uint4		ver;
	boolean_t	act_specified_in_gld;		/* this gvt's global name had its "act" specified in the .gld in its
							 * -GBLNAME section.
							 */
	boolean_t	nct_must_be_zero;		/* this gvt is part of a multi-region spanning global and hence
							 * its "nct" cannot be anything but zero.
							 */
	boolean_t	split_cleanup_needed;
	char		last_split_direction[MAX_BT_DEPTH - 1];	/* maintain last split direction for each level in the GVT */
	char		filler_8byte_align1[6];
	block_id	last_split_blk_num[MAX_BT_DEPTH - 1];
#	ifdef GTM_TRIGGER
	struct gvt_trigger_struct *gvt_trigger;		/* pointer to trigger info for this global
							 * (is non-NULL only if db_trigger_cycle is non-zero) */
	uint4		db_trigger_cycle;		/* copy of csd->db_trigger_cycle when triggers for this global were
							 * last read/initialized from ^#t global (in gvtr_init) */
	uint4		db_dztrigger_cycle;		/* copy of csa->db_dztrigger_cycle when triggers for this global were
							 * last read/initialized from ^#t global (in gvtr_init) */
	boolean_t	trig_mismatch_test_done;	/* whether update process has checked once if there is a mismatch
							 * in trigger definitions between originating and replicating instance */
	GTM64_ONLY(uint4 filler_8byte_align2;)		/* for 8-byte alignment of "clue" member. (targ_alloc relies on this) */
#	endif
	gv_key		clue;				/* Clue key, must be last in namehead struct because of hung buffer. */
} gv_namehead;

/* Below structure is allocated for every global name that spans across multiple regions in each global directory */
typedef struct gvnh_spanreg_struct
{
	int		start_map_index;	/* index into the global directory "maps" array corresponding to the
						 *	FIRST map entry with "gvkey" member containing subscripted
						 *	keys of the parent (unsubscripted) global name.
						 */
	int		end_map_index;		/* index into the global directory "maps" array corresponding to the
						 *	LAST map entry with "gvkey" member containing subscripted
						 *	keys of the parent (unsubscripted) global name.
						 *	"start_map_index" and "end_map_index" serve as two bounds of
						 *	the array within which a binary search is done to find which
						 *	map entry contains a given input key.
						 */
	int		min_reg_index;		/* index into the global directory "regions" array such that ALL
						 *	regions that the parent global name spans across lie AFTER
						 *	this index in the "regions" array.
						 */
	int		max_reg_index;		/* index into the global directory "regions" array corresponding to
						 *	the LAST region gv_target pointer for the spans of this global.
						 *	The count (max_reg_index-min_reg_index)+1 effectively determines the
						 *	size of the array allocated to store the corresponding
						 *	gv_targets for each unique region the parent global name spans.
						 */
	gv_namehead	*gvt_array[1];		/* array of gv_targets corresponding to each region the global name spans.
						 *	Although the array is defined to be size 1, the actual size allocated
						 *	depends on (max_reg_index-min_reg_index)+1 and having this defined as
						 *	an array lets us access the entire allocated size with an array index.
						 *	Set to INVALID_GV_TARGET for array indices that correspond to regions
						 *	which the parent global does NOT span across.
						 */
} gvnh_spanreg_t;

/* Below structure is allocated for every global name in each global directory
 * (irrespective of whether it spans across multiple regions or not).
 */
typedef struct	gvnh_reg_struct
{
	gv_namehead	*gvt;			/* Pointer to gv_target for the unsubscripted global name */
	gd_region	*gd_reg;		/* Region corresponding to the global directory map entry where
						 *	the unsubscripted global name was found.
						 */
	gvnh_spanreg_t	*gvspan;		/* Pointer to a structure containing details of what regions are spanned
						 *	by this global name. Set to NULL for globals that don't span regions.
						 */
	uint4		act;			/* Copy of alternative collation SEQUENCE defined in GBLNAMES section of gbldir */
	uint4		ver;			/* Copy of collation library VERSION defined in GBLNAMES section of gbldir */
} gvnh_reg_t;

#define	GVNH_REG_INIT(ADDR, HASHTAB, GD_MAP, GVT, REG, GVNH_REG, TABENT)							\
MBSTART {															\
	boolean_t	added, gbl_spans_regions;										\
	char		*gvent_name;												\
	gd_binding	*spanmap;												\
	int		res, gvent_len;												\
																\
	GBLREF	jnl_gbls_t	jgbl;												\
	GBLREF	gv_key		*gv_currkey;											\
																\
	GVNH_REG = (gvnh_reg_t *)malloc(SIZEOF(gvnh_reg_t));									\
	GVNH_REG->gvt = GVT;													\
	GVNH_REG->gd_reg = REG;													\
	/* If GD_MAP is NULL, it implies callers like MUPIP JOURNAL -RECOVER or GT.CM GNP server				\
	 * which don't have a .gld context to map input global names to regions, but instead know				\
	 * which regions to play input updates. If GD_MAP is non-NULL, it implies the caller has				\
	 * a .gld with map entries and wants to do more initialization inside this macro if the					\
	 * input global spans multiple regions. Note that it is possible that even though GD_MAP is NULL,			\
	 * ADDR could be non-NULL. This is necessary in case we need the gld file for GBLNAME section like			\
	 * for MUPIP JOURNAL -RECOVER even though it operates on a per-region basis only. The GBLNAME section			\
	 * is necessary to set correct collation properties in the directory tree if journal recover creates			\
	 * the directory tree.													\
	 */															\
	assert((NULL != ADDR) || (NULL == GD_MAP));										\
	if (NULL != GD_MAP)													\
	{	/* check if global spans multiple regions and if so initialize "gvnh_reg->gvspan" */				\
		gvnh_spanreg_init(GVNH_REG, ADDR, GD_MAP);									\
		gbl_spans_regions = (NULL != GVNH_REG->gvspan);									\
	} else															\
	{	/* GT.CM GNP or MUPIP JOURNAL -RECOVER/ROLLBACK */								\
		GVNH_REG->gvspan = NULL;											\
		/* If GT.CM GNP, value of ADDR will be NULL so no need to search the global directory.				\
		 * Otherwise (i.e. if MUPIP JOURNAL RECOVER/ROLLBACK), find from the gld whether the global			\
		 * name spans regions. This is necessary to do unconditional collation initialization (as if			\
		 * the gld specified it) for globals that span multiple regions inside the					\
		 * COPY_ACT_FROM_GLD_TO_GVNH_REG_AND_GVT macro.	An exception is ^#t (actually all globals that			\
		 * begin with ^# but ^#t is the only one currently). This does not map to a single region in			\
		 * the gld map and so gv_srch_map should never be invoked for such globals. This global can			\
		 * never span regions so treat it accordingly.									\
		 */														\
		if ((NULL == ADDR) || IS_MNAME_HASHT_GBLNAME(GVT->gvname.var_name))						\
			gbl_spans_regions = FALSE;										\
		else														\
		{														\
			assert(jgbl.forw_phase_recovery);									\
			gvent_name = GVT->gvname.var_name.addr;									\
			gvent_len = GVT->gvname.var_name.len;									\
			spanmap = gv_srch_map(ADDR, gvent_name, gvent_len, SKIP_BASEDB_OPEN_FALSE);				\
			res = memcmp(gvent_name, &(spanmap->gvkey.addr[0]), gvent_len);						\
			assert((0 != res) || (gvent_len <= spanmap->gvname_len));						\
			gbl_spans_regions = !((0 > res) || ((0 == res) && (gvent_len < spanmap->gvname_len)));			\
		}														\
	}															\
	COPY_ACT_FROM_GLD_TO_GVNH_REG_AND_GVT(ADDR, GVT, gbl_spans_regions, GVNH_REG, REG);					\
	/* Add to hash table after all potential error conditions have been checked. If it was the other way			\
	 * around, we could end up in a situation where an error is issued but pointers are set up incorrectly			\
	 * so a future global reference will no longer error out and will accept out-of-design updates.				\
	 * The only drawback of this approach is it might have a memory leak since allocated structures will			\
	 * no longer have a pointer but that is considered acceptable since these errors are very unlikely			\
	 * and the alternative (to set up condition handlers etc.) is not considered worth the effort now.			\
	 */															\
	added = add_hashtab_mname((hash_table_mname *)HASHTAB, &GVT->gvname, GVNH_REG, &TABENT);				\
	assert(added || (IS_STATSDB_REG(REG) && (STATSDB_GBLNAME_LEN == GVT->gvname.var_name.len)				\
				&& (0 == memcmp(GVT->gvname.var_name.addr, STATSDB_GBLNAME, STATSDB_GBLNAME_LEN))));		\
} MBEND

/* Below macro is used whenever we need to print region of interest in case of a global that spans multiple regions */
#define	SPANREG_REGION_LIT	" (region "
#define	SPANREG_REGION_LITLEN	STR_LIT_LEN(SPANREG_REGION_LIT)

#define INVALID_GV_TARGET (gv_namehead *)-1L
/* Below macro is used to get the "gvnh_reg->gvspan->gvt_array[]" contents taking into account some
 * might be set to INVALID_GV_TARGET (done only in DEBUG mode). In that case, we actually want to return NULL
 * as there is NO gvt defined in that slot.
 */
#ifdef DEBUG
#define	GET_REAL_GVT(gvt)	((INVALID_GV_TARGET == gvt) ? NULL : gvt)
#else
#define	GET_REAL_GVT(gvt)	gvt
#endif

typedef struct gvsavtarg_struct
{
	gd_region		*gv_cur_region;
	gv_namehead		*gv_target;
	gvnh_reg_t		*gd_targ_gvnh_reg;
	gd_binding		*gd_targ_map;
	boolean_t		gv_last_subsc_null;
	boolean_t		gv_some_subsc_null;
	uint4			prev;
	uint4			end;
} gvsavtarg_t;

#define	GVSAVTARG_ALIGN_BNDRY	8
#define	GVSAVTARG_FIXED_SIZE	(SIZEOF(gvsavtarg_t))

/* Following three macros define the mechanism to restore gv_target under normal and error conditions.
 * RESET_GV_TARGET should be used to restore gv_target from the global, reset_gv_target, only when we
 * are sure that this function is the first one in the call stack to have saved gv_target.
 * If the module that needs the restoration mechanism is not the first one to save gv_target in the call
 * stack, then one of the last two macros should be used.
 * RESET_GV_TARGET_LCL is used to restore gv_target from the local variable used to save gv_target.
 * RESET_GV_TARGET_LCL_AND_CLR_GBL is used at the end of the module, when there are no more gv_target
 * restorations needed. This resets gv_target and invalidates reset_gv_target.
 *
 * This mechanism ensures that, when there are multiple functions in a given call stack that save and
 * restore gv_target, only the bottom most function gets to store its value in the global, reset_gv_target.
 * In case of rts errors, if the error is not SUCCESS or INFO, then gv_target gets restored to reset_gv_target
 * (in preemptive_db_clnup()). For SUCCESS or INFO, no restoration is necessary because CONTINUE from the condition
 * handlers would take us through the normal path for gv_target restoration.
 */

#define	SKIP_GVT_GVKEY_CHECK		0
#define	DO_GVT_GVKEY_CHECK		1
#define	DO_GVT_GVKEY_CHECK_RESTART	2	/* do GVT_GVKEY check but skip gvt/csa check since we are in a TP transaction
						 * and about to restart, gv_target and cs_addrs will anyways get back in sync
						 * as part of the tp_restart process. This flag should be used only in TP
						 * as non-TP restart does not do this reset/sync.
						 */

#define RESET_GV_TARGET(GVT_GVKEY_CHECK)							\
MBSTART {											\
	assert(INVALID_GV_TARGET != reset_gv_target);						\
	gv_target = reset_gv_target;								\
	reset_gv_target = INVALID_GV_TARGET;							\
	if (GVT_GVKEY_CHECK)									\
		DBG_CHECK_GVTARGET_GVCURRKEY_IN_SYNC(CHECK_CSA_TRUE);				\
} MBEND

#define RESET_GV_TARGET_LCL(SAVE_TARG)									\
MBSTART {												\
	gv_target = SAVE_TARG;										\
} MBEND

#define RESET_GV_TARGET_LCL_AND_CLR_GBL(SAVE_TARG, GVT_GVKEY_CHECK)							\
MBSTART {														\
	GBLREF	uint4			dollar_tlevel;									\
															\
	gv_target = SAVE_TARG;												\
	if (!gbl_target_was_set)											\
	{														\
		assert(SAVE_TARG == reset_gv_target || INVALID_GV_TARGET == reset_gv_target);				\
		DEBUG_ONLY(												\
			if (DO_GVT_GVKEY_CHECK == (GVT_GVKEY_CHECK))							\
			{												\
				DBG_CHECK_GVTARGET_GVCURRKEY_IN_SYNC(CHECK_CSA_TRUE);					\
			} else												\
			{												\
				assert((SKIP_GVT_GVKEY_CHECK == (GVT_GVKEY_CHECK))					\
					|| (dollar_tlevel && (DO_GVT_GVKEY_CHECK_RESTART == (GVT_GVKEY_CHECK))));	\
				DBG_CHECK_GVTARGET_GVCURRKEY_IN_SYNC(CHECK_CSA_FALSE);					\
			}												\
		)													\
		reset_gv_target = INVALID_GV_TARGET;									\
	}														\
} MBEND


/* No point doing the gvtarget-gvcurrkey in-sync check or the gvtarget-csaddrs in-sync check if we are anyways going to exit.
 * There is no way op_gvname (which is where these design assumptions get actually used) is going to be called from now onwards.
 */
GBLREF	int		process_exiting;
GBLREF	trans_num	local_tn;
GBLREF	gv_namehead	*gvt_tp_list;

#define	RESET_FIRST_TP_SRCH_STATUS_FALSE	FALSE
#define	RESET_FIRST_TP_SRCH_STATUS_TRUE		TRUE

#define	GVT_CLEAR_FIRST_TP_SRCH_STATUS(GVT)								\
MBSTART {												\
	srch_blk_status	*srch_status;									\
													\
	assert(GVT->clue.end);	/* or else first_tp_srch_status will be reset as part of traversal */	\
	assert(GVT->read_local_tn != local_tn);								\
	for (srch_status = &(GVT)->hist.h[0]; HIST_TERMINATOR != srch_status->blk_num; srch_status++)	\
		srch_status->first_tp_srch_status = NULL;						\
} MBEND

#define	ADD_TO_GVT_TP_LIST(GVT, RESET_FIRST_TP_SRCH_STATUS)									\
MBSTART {															\
	if (GVT->read_local_tn != local_tn)											\
	{	/* Set read_local_tn to local_tn; Also add GVT to list of gvtargets referenced in this TP transaction. */	\
		if (GVT->clue.end && RESET_FIRST_TP_SRCH_STATUS)								\
			GVT_CLEAR_FIRST_TP_SRCH_STATUS(GVT);									\
		GVT->read_local_tn = local_tn;											\
		GVT->next_tp_gvnh = gvt_tp_list;										\
		gvt_tp_list = GVT;												\
	} else															\
	{	/* Check that GVT is already part of the list of gvtargets referenced in this TP transaction */			\
		DBG_CHECK_IN_GVT_TP_LIST(GVT, TRUE);	/* TRUE => we check that GVT IS present in the gvt_tp_list */		\
	}															\
} MBEND

/* Although the below macros are used only in DBG code, they are passed as parameters so need to be defined for pro code too */
#define	CHECK_CSA_FALSE		FALSE
#define	CHECK_CSA_TRUE		TRUE

#ifdef DEBUG
#define	DBG_CHECK_IN_GVT_TP_LIST(gvt, present)						\
MBSTART {										\
	gv_namehead	*gvtarg;							\
											\
	GBLREF gv_namehead	*gvt_tp_list;						\
	GBLREF  uint4           dollar_tlevel;						\
											\
	for (gvtarg = gvt_tp_list; NULL != gvtarg; gvtarg = gvtarg->next_tp_gvnh)	\
	{										\
		if (gvtarg == gvt)							\
			break;								\
	}										\
	assert(!present || (NULL != gvtarg));						\
	assert(present || (NULL == gvtarg) || (process_exiting && !dollar_tlevel));	\
} MBEND

#define	DBG_CHECK_GVT_IN_GVTARGETLIST(gvt)								\
MBSTART {												\
	gv_namehead	*gvtarg;									\
													\
	GBLREF gd_region	*gv_cur_region;								\
	GBLREF gv_namehead	*gv_target_list;							\
													\
	for (gvtarg = gv_target_list; NULL != gvtarg; gvtarg = gvtarg->next_gvnh)			\
	{												\
		if (gvtarg == gvt)									\
			break;										\
	}												\
	/* For dba_cm or dba_usr type of regions, gv_target_list is not maintained so			\
	 * if gv_target is not part of gv_target_list, assert region is not BG or MM.			\
	 * The only exception is if the region was dba_cm but later closed due to an error on		\
	 * the server side (in which case access method gets reset back to BG. (e.g. gvcmz_error.c)	\
	 */												\
	assert((NULL != gvtarg) || (dba_cm == REG_ACC_METH(gv_cur_region))				\
		|| (dba_usr == REG_ACC_METH(gv_cur_region))						\
		|| ((FALSE == gv_cur_region->open) && (dba_bg == REG_ACC_METH(gv_cur_region))));	\
} MBEND

/* If CHECK_CSADDRS input parameter is CHECK_CSA_TRUE, then check that GV_CURRKEY, GV_TARGET and CS_ADDRS are all in sync.
 * If CHECK_CSADDRS input parameter is CHECK_CSA_FALSE, then only check GV_CURRKEY and GV_TARGET are in sync (skip CS_ADDRS check).
 * The hope is that most callers of this macro use CHECK_CSA_TRUE (i.e. a stricter check).
 *
 * The DBG_CHECK_GVTARGET_GVCURRKEY_IN_SYNC(CHECK_CSADDRS) macro is used at various points in the database code to check that
 * gv_currkey, gv_target and cs_addrs are in sync. This is because op_gvname relies on this in order to avoid a gv_bind_name
 * function call (if incoming key matches gv_currkey from previous call, it uses gv_target and cs_addrs right
 * away instead of recomputing them). The only exception is if we were interrupted in the middle of TP transaction by an
 * external signal which resulted in us terminating right away. In this case, we are guaranteed not to make a call to op_gvname
 * again (because we are exiting) so it is ok not to do this check if process_exiting is TRUE.
 *
 * Update: The above comment is no longer true. As part of GTM-2168, the fast path in op_gvname was removed but we have not
 * removed code that does the below macro invocation since this is dbg-only and is anyways otherwise true. If it is a hassle
 * ensuring the below anywhere, it should be okay to remove it then.
 */
#define	DBG_CHECK_GVTARGET_GVCURRKEY_IN_SYNC(CHECK_CSADDRS)									\
MBSTART {															\
	mname_entry		*gvent;												\
	mstr			*varname;											\
	int			varlen;												\
	unsigned short		keyend;												\
	unsigned char		*keybase;											\
																\
	GBLREF int4		gv_keysize;											\
																\
	GBLREF gv_key		*gv_currkey;											\
	GBLREF gv_namehead	*reset_gv_target;										\
																\
	assert((NULL != gv_currkey) || (NULL == gv_target));									\
	/* Make sure gv_currkey->top always reflects the maximum keysize across all dbs that we opened until now */		\
	assert((NULL == gv_currkey) || (gv_currkey->top == gv_keysize));							\
	if (!process_exiting)													\
	{															\
		keybase = &gv_currkey->base[0];											\
		if ((NULL != gv_currkey) && (0 != keybase[0]) && (0 != gv_currkey->end)						\
				&& (INVALID_GV_TARGET == reset_gv_target))							\
		{														\
			assert(NULL != gv_target);										\
			gvent = &gv_target->gvname;										\
			varname = &gvent->var_name;										\
			varlen = varname->len;											\
			assert(varlen);												\
			assert((0 != keybase[varlen]) || !memcmp(keybase, varname->addr, varlen));				\
			keyend = gv_currkey->end;										\
			assert(!keyend || (KEY_DELIMITER == keybase[keyend]));							\
			assert(!keyend || (KEY_DELIMITER == keybase[keyend - 1]));						\
			/* Check that gv_target is part of the gv_target_list */						\
			DBG_CHECK_GVT_IN_GVTARGETLIST(gv_target);								\
			if (CHECK_CSADDRS)											\
				DBG_CHECK_GVTARGET_CSADDRS_IN_SYNC;								\
		}														\
		/* Do gv_target sanity check too; Do not do this if it is NULL or if it is GT.CM GNP client (gd_csa is NULL) */	\
		if ((NULL != gv_target) && (NULL != gv_target->gd_csa))								\
			DBG_CHECK_GVTARGET_INTEGRITY(gv_target);								\
	}															\
} MBEND

/* Do checks on the integrity of various fields in gv_target. targ_alloc initializes these and they are supposed to
 * stay that way. The following code is very similar to that in targ_alloc so needs to be maintained in sync. This
 * macro expects that gv_target->gd_csa is non-NULL (could be NULL for GT.CM GNP client) so any callers of this macro
 * should ensure they do not invoke it in case of NULL gd_csa.
 */
#define	DBG_CHECK_GVTARGET_INTEGRITY(GVT)											\
MBSTART {															\
	int			keysize, partial_size;										\
	GBLREF	boolean_t	dse_running;											\
																\
	if (NULL != GVT->gd_csa->nl)												\
	{	/* csa->nl is cleared when a statsDB is closed due to opt-out so use as flag if DB is open or not */		\
		keysize = GVT->gd_csa->hdr->max_key_size;									\
		keysize = DBKEYSIZE(keysize);											\
		partial_size = SIZEOF(gv_namehead) + 2 * SIZEOF(gv_key) + 3 * keysize;						\
		/* DSE could change the max_key_size dynamically so account for it in the below assert */			\
		if (!dse_running)												\
		{														\
			assert(GVT->gvname.var_name.addr == (char *)GVT + partial_size);					\
			assert((char *)GVT->first_rec == ((char *)&GVT->clue + SIZEOF(gv_key) + keysize));			\
			assert((char *)GVT->last_rec  == ((char *)GVT->first_rec + SIZEOF(gv_key) + keysize));			\
			assert(GVT->clue.top == keysize);									\
		}														\
		assert(GVT->clue.top == GVT->first_rec->top);									\
		assert(GVT->clue.top == GVT->last_rec->top);									\
	}															\
} MBEND

/* Do checks on the integrity of GVKEY */
#	define	DBG_CHECK_GVKEY_VALID(GVKEY)					\
MBSTART {									\
	unsigned char	ch, prevch, *ptr, *pend;				\
										\
	assert(GVKEY->end < GVKEY->top);					\
	ptr = &GVKEY->base[0];							\
	pend = ptr + GVKEY->end;						\
	assert(KEY_DELIMITER == *pend);						\
	assert((ptr == pend) || (KEY_DELIMITER == *(pend - 1)));		\
	prevch = KEY_DELIMITER;							\
	while (ptr < pend)							\
	{									\
		ch = *ptr++;							\
		assert((KEY_DELIMITER != prevch) || (KEY_DELIMITER != ch));	\
		prevch = ch;							\
	}									\
	/* Do not check GVKEY->prev as it is usually not set. */		\
} MBEND

#else
#	define	DBG_CHECK_IN_GVT_TP_LIST(gvt, present)
#	define	DBG_CHECK_GVT_IN_GVTARGETLIST(gvt)
#	define	DBG_CHECK_GVTARGET_GVCURRKEY_IN_SYNC(CHECK_CSADDRS)
#	define	DBG_CHECK_GVTARGET_INTEGRITY(GVT)
#	define	DBG_CHECK_GVKEY_VALID(GVKEY)
#endif

/* The below GBLREFs are for the following macro */
GBLREF	gv_namehead	*gv_target;
GBLREF	sgmnt_addrs	*cs_addrs;
#define	DBG_CHECK_GVTARGET_CSADDRS_IN_SYNC	assert(process_exiting || (NULL == gv_target) || (gv_target->gd_csa == cs_addrs))

/* Indicate incompleteness of (potentially subscripted) global name by adding a "*" (without closing ")") at the end */
#define	GV_SET_LAST_SUBSCRIPT_INCOMPLETE(BUFF, END)			\
MBSTART {								\
	if (NULL == (char *)(END))					\
	{	/* The buffer passed to format_targ_key was not enough	\
		 * for the transformation. We don't expect this. Handle	\
		 * it nevertheless by adding ",*" at end.		\
		 */							\
		assert(FALSE);						\
		END = ((unsigned char *)ARRAYTOP(BUFF)) - 1;		\
		assert((char *)(END) > (char *)(BUFF));			\
		*(END)++ = '*';						\
	} else								\
	{	/* Overflow occurred while adding the global name OR	\
		 * after adding the last subscript OR in the middle of	\
		 * adding a subscript (not necessarily last). In all	\
		 * cases, add a '*' at end to indicate incompleteness.	\
		 */							\
		if (')' == END[-1])					\
			(END)--;					\
		/* ensure we have space to write 1 byte */		\
		assert((char *)(END) + 1 <= ((char *)ARRAYTOP(BUFF)));	\
		*(END)++ = '*';						\
	}								\
} MBEND

#define	KEY_COMPLETE_FALSE	FALSE
#define	KEY_COMPLETE_TRUE	TRUE

#define	ISSUE_GVSUBOFLOW_ERROR(GVKEY, IS_KEY_COMPLETE)							\
MBSTART {												\
	GBLREF	gv_key	*gv_currkey;									\
	unsigned char	*endBuff, fmtBuff[MAX_ZWR_KEY_SZ];						\
													\
	/* Assert that input key to format_targ_key is double null terminated */			\
	assert(KEY_DELIMITER == GVKEY->base[GVKEY->end]);						\
	endBuff = format_targ_key(fmtBuff, ARRAYSIZE(fmtBuff), GVKEY, TRUE);				\
	if (!IS_KEY_COMPLETE)										\
		GV_SET_LAST_SUBSCRIPT_INCOMPLETE(fmtBuff, endBuff); /* Note: might update "endBuff" */	\
	if (GVKEY == gv_currkey)									\
		gv_currkey->end = 0;	/* to show the key is not valid */				\
	rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_GVSUBOFLOW, 0, ERR_GVIS, 2,			\
			endBuff - fmtBuff, fmtBuff);							\
} MBEND

#define COPY_SUBS_TO_GVCURRKEY(mvarg, reg, gv_currkey, was_null, is_null)					\
MBSTART {													\
	GBLREF mv_stent		*mv_chain;									\
	GBLREF unsigned char	*msp, *stackwarn, *stacktop;							\
	mval			temp;										\
	unsigned char		buff[MAX_ZWR_KEY_SZ], *end;							\
	int			len;										\
	mstr			opstr;										\
														\
	was_null |= is_null;											\
	if (mvarg->mvtype & MV_SUBLIT)										\
	{													\
		is_null = ((STR_SUB_PREFIX == *(unsigned char *)mvarg->str.addr)				\
					&& (KEY_DELIMITER == *(mvarg->str.addr + 1))); 				\
		if (gv_target->collseq || gv_target->nct)							\
		{												\
			/* collation transformation should be done at the server's end for CM regions */	\
			assert(dba_cm != REG_ACC_METH(reg));							\
			TREF(transform) = FALSE;								\
			opstr.addr = (char *)buff;								\
			opstr.len = MAX_ZWR_KEY_SZ;								\
			end = gvsub2str((uchar_ptr_t)mvarg->str.addr, &opstr, FALSE);				\
			TREF(transform) = TRUE;									\
			temp.mvtype = MV_STR;									\
			temp.str.addr = (char *)buff;								\
			temp.str.len = (mstr_len_t)(end - buff);						\
			mval2subsc(&temp, gv_currkey, reg->std_null_coll);					\
		} else												\
		{												\
			len = mvarg->str.len;									\
			if (gv_currkey->end + len - 1 >= gv_currkey->top)					\
				ISSUE_GVSUBOFLOW_ERROR(gv_currkey, KEY_COMPLETE_FALSE);				\
			memcpy((gv_currkey->base + gv_currkey->end), mvarg->str.addr, len);			\
			if (is_null && 0 != reg->std_null_coll)							\
				gv_currkey->base[gv_currkey->end] = SUBSCRIPT_STDCOL_NULL;			\
			gv_currkey->prev = gv_currkey->end;							\
			gv_currkey->end += len - 1;								\
		}												\
	} else													\
	{													\
		MV_FORCE_DEFINED(mvarg);									\
		mval2subsc(mvarg, gv_currkey, reg->std_null_coll);						\
		is_null = (MV_IS_STRING(mvarg) && (0 == mvarg->str.len));					\
	}													\
} MBEND

#define	EXPAND_PREV_KEY_FALSE	FALSE
#define	EXPAND_PREV_KEY_TRUE	TRUE

/* Need a special value to indicate the prev_key was not computed in the last gvcst_search in a clue.
 * Store an impossible keysize value as the key->end there. The below macro computes such a value.
 */
#define	PREV_KEY_NOT_COMPUTED	DBKEYSIZE(MAX_KEY_SZ)

#define	COPY_PREV_KEY_TO_GVT_CLUE(GVT, EXPAND_PREV_KEY)							\
MBSTART {												\
	GBLREF gv_key	*gv_altkey;									\
													\
	if (EXPAND_PREV_KEY)										\
	{	/* gv_altkey has the previous key. Store it in clue for future clue-based searches */	\
		if (NULL == GVT->prev_key)								\
			GVKEY_INIT(GVT->prev_key, GVT->clue.top);					\
		if (gv_altkey->end >= GVT->prev_key->top)						\
		{	/* Note that this is possible in case of concurrency issues (i.e. we are in	\
			 * a restartable situation (see comment at bottom of gvcst_expand_key.c which	\
			 * talks about a well-formed key. Since we cannot easily signal a restart here,	\
			 * we reallocate to ensure the COPY_KEY does not cause a buffer overflow and	\
			 * the caller will eventually do the restart.					\
			 */										\
			DEBUG_ONLY(TREF(donot_commit) |= DONOTCOMMIT_COPY_PREV_KEY_TO_GVT_CLUE;)	\
			GVKEY_INIT(GVT->prev_key, DBKEYSIZE(gv_altkey->end));				\
		}											\
		COPY_KEY(GVT->prev_key, gv_altkey);							\
	} else if (NULL != GVT->prev_key)								\
	{												\
		assert(PREV_KEY_NOT_COMPUTED < (1 << (SIZEOF(gv_altkey->end) * 8)));			\
		GVT->prev_key->end = PREV_KEY_NOT_COMPUTED;						\
	}												\
} MBEND

/* Copy GVKEY to GVT->CLUE. Take care NOT to copy cluekey->top to GVKEY->top as they correspond
 * to the allocation sizes of two different memory locations and should stay untouched.
 */
#define	COPY_CURR_AND_PREV_KEY_TO_GVTARGET_CLUE(GVT, GVKEY, EXPAND_PREV_KEY)				\
MBSTART {												\
	GBLREF gv_key	*gv_altkey;									\
	int		keyend;										\
	DCL_THREADGBL_ACCESS;										\
													\
	SETUP_THREADGBL_ACCESS;										\
	keyend = GVKEY->end;										\
	if (GVT->clue.top <= keyend)									\
	{	/* Possible only if GVT corresponds to a global that spans multiple			\
		 * regions. For example, a gvcst_spr_* function could construct a			\
		 * gv_currkey starting at one spanned region and might have to do a			\
		 * gvcst_* operation on another spanned region with a max-key-size			\
		 * that is smaller than gv_currkey->end. In that case, copy only the			\
		 * portion of gv_currkey that will fit in the gvt of the target region.			\
		 */											\
		assert(TREF(spangbl_seen));								\
		keyend = GVT->clue.top - 1;								\
		memcpy(GVT->clue.base, GVKEY->base, keyend - 1);					\
		GVT->clue.base[keyend - 1] = KEY_DELIMITER;						\
		GVT->clue.base[keyend] = KEY_DELIMITER;							\
	} else												\
	{												\
		assert(KEY_DELIMITER == GVKEY->base[keyend]);						\
		assert(KEY_DELIMITER == GVKEY->base[keyend - 1]);					\
		memcpy(GVT->clue.base, GVKEY->base, keyend + 1);					\
	}												\
	GVT->clue.end = keyend;										\
	/* No need to maintain unused GVT->clue.prev */							\
	COPY_PREV_KEY_TO_GVT_CLUE(GVT, EXPAND_PREV_KEY);						\
	DBG_CHECK_GVTARGET_INTEGRITY(GVT);								\
} MBEND

/* Need a special value to indicate the prev_rec.match and prev_rec.offset were not initialized in the
 * index block search history as part of the most recent gvcst_search call. Store an impossible
 * prev_rec.offset (and in turn prev_rec.match) value. Since they both are unsigned short quantities,
 * and since sizeof(rec_hdr) is 4 bytes, prev_rec.offset can only be less than 64K-4. And so 64K-1
 * (i.e. MAXUINT2) serves as a good impossible value for both.
 */
#define	PREV_REC_UNINITIALIZED	((unsigned short)MAXUINT2)

/* Note that anytime bh->prev_rec.offset or bh->prev_rec.match is used in the code, one needs to prefix that
 * usage with either an ASSERT_LEAF_BLK_PREV_REC_INITIALIZED or ASSERT_PREV_REC_INITIALIZED or INITIALIZE_PREV_REC_IF_NEEDED
 * macro call. This is needed to handle the case where the offset/match could take on the special value PREV_REC_UNINITIALIZED
 * in case of a special call path through $zprevious(^x("")). If the caller knows for sure it is leaf level block, then it
 * should use ASSERT_LEAF_BLK_PREV_REC_INITIALIZED. If the caller knows prev_rec is initialized but not guaranteed it is a
 * leaf level block, it should use ASSERT_PREV_REC_INITIALIZED. If neither, then it needs to use INITIALIZE_PREV_REC_IF_NEEDED.
 * Currently this neither case is not possible (explanations in respective places where one of the above 2 asserts are used).
 * So INITIALIZE_PREV_REC_IF_NEEDED macro is unused. Nevertheless, its definition is left alone to help when the first need arises.
 */
#define	ASSERT_PREV_REC_INITIALIZED(PREVREC)			\
{								\
	assert(PREV_REC_UNINITIALIZED != PREVREC.offset);	\
	assert(PREV_REC_UNINITIALIZED != PREVREC.match);	\
}

#define	ASSERT_LEAF_BLK_PREV_REC_INITIALIZED(SRCHBLKSTATUS)	\
MBSTART {							\
	assert(0 == SRCHBLKSTATUS->level);			\
	ASSERT_PREV_REC_INITIALIZED(SRCHBLKSTATUS->prev_rec);	\
} MBEND

/* If prev_rec.match or prev_rec.offset are PREV_REC_UNINITIALIZED, then invoke "gvcst_search_blk" to initialize
 * that. If failure during that, then invoke code fragment passed in as "STMT" variable.
 */
#define	INITIALIZE_PREV_REC_IF_NEEDED(SRCHBLKSTATUS, SRCHKEY, SRCHSTATUS, STMT)			\
{												\
	assert((PREV_REC_UNINITIALIZED != SRCHBLKSTATUS->prev_rec.match)			\
		|| (PREV_REC_UNINITIALIZED == SRCHBLKSTATUS->prev_rec.offset));			\
	assert((PREV_REC_UNINITIALIZED != SRCHBLKSTATUS->prev_rec.offset)			\
		|| (PREV_REC_UNINITIALIZED == SRCHBLKSTATUS->prev_rec.match));			\
	if (PREV_REC_UNINITIALIZED == SRCHBLKSTATUS->prev_rec.match)				\
	{											\
		SRCHSTATUS = gvcst_search_blk(SRCHKEY, SRCHBLKSTATUS);				\
		assert((cdb_sc_normal != SRCHSTATUS)						\
			|| ((PREV_REC_UNINITIALIZED != SRCHBLKSTATUS->prev_rec.offset)		\
				&& (PREV_REC_UNINITIALIZED != SRCHBLKSTATUS->prev_rec.match)));	\
		if (cdb_sc_normal != SRCHSTATUS)						\
		{										\
			assert(CDB_STAGNATE > t_tries);						\
			STMT;									\
		}										\
	}											\
}

/* If SRC_KEY->end == 0, make sure to copy the first byte of SRC_KEY->base */
#define MEMCPY_KEY(TARG_KEY, SRC_KEY)								\
MBSTART {											\
	memcpy((TARG_KEY), (SRC_KEY), OFFSETOF(gv_key, base[0]) + (SRC_KEY)->end + 1);		\
} MBEND
#define COPY_KEY(TARG_KEY, SRC_KEY)												\
MBSTART {															\
	assert(TARG_KEY->top >= SRC_KEY->end);											\
	/* ensure proper alignment before dereferencing SRC_KEY->end */								\
	assert(0 == (((UINTPTR_T)(SRC_KEY)) % SIZEOF(SRC_KEY->end)));								\
	/* WARNING: depends on the first two bytes of gv_key structure being key top field */					\
	assert((2 == SIZEOF(TARG_KEY->top)) && ((sm_uc_ptr_t)(TARG_KEY) == (sm_uc_ptr_t)(&TARG_KEY->top)));			\
	memcpy(((sm_uc_ptr_t)(TARG_KEY) + 2), ((sm_uc_ptr_t)(SRC_KEY) + 2), OFFSETOF(gv_key, base[0]) + (SRC_KEY)->end - 1);	\
} MBEND

/* Macro to denote special value of first_rec when it is no longer reliable */
#define	GVT_CLUE_FIRST_REC_UNRELIABLE	(short)0xffff

/* Macro to denote special value of last_rec when it is the absolute maximum (in case of *-keys all the way down) */
#define	GVT_CLUE_LAST_REC_MAXKEY	(short)0xffff

/* Macro to reset first_rec to a special value to indicate it is no longer reliable
 * (i.e. the keyrange [first_rec, clue] should not be used by gvcst_search.
 * Note that [clue, last_rec] is still a valid keyrange and can be used by gvcst_search.
 */
#define	GVT_CLUE_INVALIDATE_FIRST_REC(GVT)						\
MBSTART {										\
	gv_key *firstRec;								\
											\
	assert(GVT->clue.end);								\
	firstRec = GVT->first_rec;							\
	*((short *)firstRec->base) = GVT_CLUE_FIRST_REC_UNRELIABLE;			\
	/* The below lines are needed by an assert in DEBUG_GVT_CLUE_VALIDATE macro	\
	 * In pro, we only use first_rec->base and don't use first_rec->end and since	\
	 * the first byte is 0xff (greater than first valid mname which is an alphabet)	\
	 * we are guaranteed the memcmp will stop right after the first byte.		\
	 */										\
	DEBUG_ONLY(firstRec->base[2] = KEY_DELIMITER);					\
	DEBUG_ONLY(firstRec->base[3] = KEY_DELIMITER);					\
	DEBUG_ONLY(firstRec->end = 3);							\
} MBEND

#ifdef DEBUG
/* Macro to check that the clue is valid. Basically check that first_rec <= clue <= last_rec. Also check that
 * all of them start with the same global name in case of a GVT. A clue that does not satisfy these validity
 * checks implies the possibility of DBKEYORD errors (e.g. C9905-001119 in VMS).
 */
#define	DEBUG_GVT_CLUE_VALIDATE(GVT)											\
MBSTART {														\
	mname_entry		*gvent;											\
	unsigned short		klen;											\
	gv_namehead		*gvt;											\
															\
	/* Verify that clue->first_rec <= clue.base <= clue->last_rec.							\
	 * The only exception is if first_rec has been reset to an unreliable value.					\
	 */														\
	gvt = GVT; /* copy into local variable to avoid evaluating input multiple times */				\
	klen = MIN(gvt->clue.end, gvt->first_rec->end);									\
	assert(klen);													\
	assert((0 <= memcmp(gvt->clue.base, gvt->first_rec->base, klen))						\
		|| (GVT_CLUE_FIRST_REC_UNRELIABLE == *((short *)gvt->first_rec->base)));				\
	klen = MIN(gvt->clue.end, gvt->last_rec->end);									\
	assert(klen);													\
	assert(0 <= memcmp(gvt->last_rec->base, gvt->clue.base, klen));							\
	if (DIR_ROOT != gvt->root)											\
	{	/* Not a directory tree => a GVT tree, check that first_rec/last_rec have at least gvname in it */	\
		gvent = &gvt->gvname;											\
		if (GVT_CLUE_FIRST_REC_UNRELIABLE != *((short *)gvt->first_rec->base))					\
		{													\
			assert((0 == memcmp(gvent->var_name.addr, gvt->first_rec->base, gvent->var_name.len))		\
				&& (KEY_DELIMITER == gvt->first_rec->base[gvent->var_name.len]));			\
		}													\
		if (GVT_CLUE_LAST_REC_MAXKEY != *((short *)gvt->last_rec->base))					\
		{													\
			assert((0 == memcmp(gvent->var_name.addr, gvt->last_rec->base, gvent->var_name.len))		\
				&& (KEY_DELIMITER == gvt->last_rec->base[gvent->var_name.len]));			\
		}													\
	}														\
} MBEND
#else
#define	DEBUG_GVT_CLUE_VALIDATE(GVT)
#endif

#define	ZPREVIOUS_NULL_SUBS_LEVEL1	2	/* this value should be != FALSE and TRUE and is relied upon by "gvcst_search" */

/* Macro to replace a NULL subscript at the end with the maximum possible subscript that could exist in the
 * database for this global name. Used by $ZPREVIOUS, $QUERY(gvn,-1) etc.
 */
#define GV_APPEND_MAX_SUBS_KEY(GVKEY, GVT)							\
MBSTART {											\
	int		lastsubslen, keysize;							\
	unsigned char	*ptr;									\
												\
	assert(GVT->clue.top || (NULL == GVT->gd_csa));						\
	assert(!GVT->clue.top || (NULL != GVT->gd_csa) && (GVT->gd_csa == cs_addrs));		\
	/* keysize can be obtained from GVT->clue.top in case of GT.M.				\
	 * But for GT.CM client, clue will be uninitialized. So we would need to		\
	 * compute keysize from gv_cur_region->max_key_size. Since this is true for		\
	 * GT.M as well, we use the same approach for both to avoid an if check and a		\
	 * break in the pipeline.								\
	 */											\
	keysize = DBKEYSIZE(gv_cur_region->max_key_size);					\
	assert(!GVT->clue.top || (keysize == GVT->clue.top));					\
	lastsubslen = keysize - GVKEY->prev - 2;						\
	assertpro((0 < lastsubslen) && (GVKEY->top >= keysize) && (GVKEY->end > GVKEY->prev));	\
	ptr = &GVKEY->base[GVKEY->prev];							\
	memset(ptr, STR_SUB_MAXVAL, lastsubslen);						\
	ptr += lastsubslen;									\
	*ptr++ = KEY_DELIMITER;	 /* terminator for last subscript */				\
	*ptr = KEY_DELIMITER;    /* terminator for entire key */				\
	GVKEY->end = GVKEY->prev + lastsubslen + 1;						\
	assert(GVKEY->end == (ptr - &GVKEY->base[0]));						\
	if (NULL != gv_target->gd_csa)								\
		DBG_CHECK_GVTARGET_INTEGRITY(GVT);						\
} MBEND

#define	GTCMTR_SUBS2STR_XFORM_IF_NEEDED(GVT, GVKEY, KEY_TOP)						\
{													\
	unsigned char	*kprev, *kcur, *ktop;								\
	boolean_t	last_sub_is_null;								\
													\
	if (GVT->collseq || GVT->nct)									\
	{	/* Need to convert subscript representation from client side to string representation	\
		 * so any collation transformations can happen on server side.				\
		 * First check if last subscript of incoming key is a NULL subscript.			\
		 * If so, client would have represented it using a sequence of FF, FF, FF, ...		\
		 * Remove the representation temporarily before doing the gv_xform_key.			\
		 * Introduce the NULL subscript after the transformation.				\
		 * This is because we do NOT allow a null subsc to be transformed to a non null subsc	\
		 * 	so no need for that be part of the transformation.				\
		 */											\
		last_sub_is_null = TRUE;								\
		kprev = &GVKEY->base[GVKEY->prev];							\
		for (kcur = kprev, ktop = &GVKEY->base[KEY_TOP] - 1; kcur < ktop; kcur++)		\
		{											\
			if (STR_SUB_MAXVAL != *kcur)							\
			{										\
				last_sub_is_null = FALSE;						\
				break;									\
			}										\
		}											\
		if (last_sub_is_null)									\
		{											\
			*kprev = KEY_DELIMITER;	/* remove the null subscript temporarily */		\
			GVKEY->end = GVKEY->prev;							\
		}											\
		gv_xform_key(GVKEY, FALSE);	/* do collation transform on server side */		\
		if (last_sub_is_null)									\
			GV_APPEND_MAX_SUBS_KEY(GVKEY, GVT); /* Insert the NULL subscript back */	\
	}												\
}

#define	GV_UNDO_APPEND_MAX_SUBS_KEY(GVKEY, REG)						\
MBSTART {										\
	assert(REG->std_null_coll || (STR_SUB_PREFIX == GVKEY->base[GVKEY->prev]));	\
	if (REG->std_null_coll)								\
		GVKEY->base[GVKEY->prev] = SUBSCRIPT_STDCOL_NULL;			\
	GVKEY->base[GVKEY->prev + 1] = KEY_DELIMITER;					\
	GVKEY->end = GVKEY->prev + 2;							\
	GVKEY->base[GVKEY->end] = KEY_DELIMITER;					\
} MBEND

/* Bit masks for the update_trans & si->update_trans variables */
#define	UPDTRNS_DB_UPDATED_MASK		(1 << 0)	/* 1 if this region was updated by this non-TP/TP transaction */
#define	UPDTRNS_JNL_LOGICAL_MASK	(1 << 1)	/* 1 if logical jnl record was written in this region's
	          					 * journal file by this TP transaction. Maintained only for TP.
							 */
#define	UPDTRNS_JNL_REPLICATED_MASK	(1 << 2)	/* 1 if there is at least one logical jnl record written in this
							 * region's journal file by this TP transaction that needs to be
							 * replicated across. 0 if all updates done to this region was
							 * inside of a trigger. Maintained only for TP.
							 */
#define	UPDTRNS_TCOMMIT_STARTED_MASK	(1 << 3)	/* 1 if non-TP or TP transaction is beyond the point of rolling
							 * back by "t_commit_cleanup" and can only be rolled forward by
							 * "secshr_db_clnup".
							 */
#define UPDTRNS_ZTRIGGER_MASK		(1 << 4)	/* 1 if ZTRIGGER command was done in this transaction. This allows
							 * the transaction to be committed even if it had no updates.
							 * Maintained only for TP.
							 */
#define	UPDTRNS_VALID_MASK		(UPDTRNS_DB_UPDATED_MASK | UPDTRNS_JNL_LOGICAL_MASK		\
					 | UPDTRNS_JNL_REPLICATED_MASK | UPDTRNS_TCOMMIT_STARTED_MASK	\
					 | UPDTRNS_ZTRIGGER_MASK)

/* The enum codes below correspond to code-paths that can increment the database curr_tn
 * without having a logical update. Journaling currently needs to know all such code-paths */
typedef enum
{
	inctn_invalid_op = 0,		/*  0 : */
	/* the following opcodes do NOT populate the global variable "inctn_detail" */
	inctn_gvcstput_extra_blk_split,	/*  1 : */
	inctn_mu_reorg,			/*  2 : */
	inctn_wcs_recover,		/*  3 : */
	/* the following opcodes populate "inctn_detail.blks2upgrd_struct" */
	inctn_gdsfilext_gtm,		/*  4 : */
	inctn_gdsfilext_mu_reorg,	/*  5 : */
	inctn_db_format_change,		/*  6 : written when cs_data->desired_db_format changes */
	/* the following opcodes populate "inctn_detail.blknum_struct" */
	inctn_bmp_mark_free_gtm,	/*  7 : */
	inctn_bmp_mark_free_mu_reorg,	/*  8 : */
	inctn_blkmarkfree,		/*  9 : a RECYCLED block being marked free by MUPIP REORG UPGRADE/DOWNGRADE */
	inctn_blkupgrd,			/* 10 : written whenever a GDS block is upgraded by MUPIP REORG UPGRADE if
					 *      a) SAFEJNL is specified OR
					 *      b) NOSAFEJNL is specified and the block is not undergoing a fmt change
					 */
	inctn_blkupgrd_fmtchng,		/* 11 : written whenever a GDS block is upgraded by MUPIP REORG UPGRADE -NOSAFEJNL
					 *      and if that block is undergoing a fmt change i.e. (V4 -> V5) OR (V5 -> V4).
					 *      This differentiation (inctn_blkupgrd vs inctn_blkupgrd_fmtch) is necessary
					 *      because in the latter case we will not be writing a PBLK record and hence
					 *      have no record otherwise of a block fmt change if it occurs (note that a
					 *      PBLK journal record's "ondsk_blkver" field normally helps recovery
					 *      determine if a fmt change occurred or not).
					 */
	inctn_blkdwngrd,		/* 12 : similar to inctn_blkupgrd except that this is for DOWNGRADE */
	inctn_blkdwngrd_fmtchng,	/* 13 : similar to inctn_blkupgrd_fmtchng except that this is for DOWNGRADE */
	inctn_blkreencrypt,		/* 14 : written whenever a GDS block is (re)encrypted using MUPIP REORG -ENCRYPT */
	inctn_jnlphase2salvage,		/* 15 : used by "jnl_phase2_salvage" to salvage a dead phase2 jnl commit */
	/* the following opcodes do NOT populate the global variable "inctn_detail" */
	inctn_opcode_total		/* 16 : MAX. All additions of inctn opcodes should be done BEFORE this line */
} inctn_opcode_t;

/* macros to check curr_tn */
#define MAX_TN_V4	((trans_num)(MAXUINT4 - TN_HEADROOM_V4))
#define MAX_TN_V6	(MAXUINT8 - TN_HEADROOM_V6)
#define TN_INVALID	(MAXUINT8)	/* impossible db tn */
#define TN_HEADROOM_V4	(2 * MAXTOTALBLKS_V4)
#define TN_HEADROOM_V6	(2 * MAXTOTALBLKS_V6)
#define	HEADROOM_FACTOR	4

/* the following macro checks that curr_tn < max_tn_warn <= max_tn.
 * if not, it adjusts max_tn_warn accordingly to ensure the above.
 * if not possible, it issues TNTOOLARGE error.
 */
#define CHECK_TN(CSA, CSD, TN)												\
MBSTART {														\
	assert((CSA)->hdr == (CSD));											\
	assert((TN) <= (CSD)->max_tn_warn);										\
	assert((CSD)->max_tn_warn <= (CSD)->max_tn);									\
	assert((CSA)->now_crit);	/* Must be crit to mess with stuff */						\
	if ((TN) >= (CSD)->max_tn_warn)											\
	{														\
		trans_num trans_left;											\
															\
		if ((CSA)->hdr->max_tn <= (TN))										\
		{													\
			rts_error_csa(CSA_ARG(CSA) VARLSTCNT(5) ERR_TNTOOLARGE, 3, DB_LEN_STR((CSA)->region),		\
					&(CSA)->hdr->max_tn);								\
			assert(FALSE);	/* should not come here */							\
		}													\
		assert((CSD)->max_tn > (TN));										\
		trans_left = (CSD)->max_tn - (TN);									\
		send_msg_csa(CSA_ARG(CSA) VARLSTCNT(6) ERR_TNWARN, 4, DB_LEN_STR((CSA)->region), &trans_left,		\
				&(CSD)->max_tn);									\
		(CSD)->max_tn_warn = (TN) + 1 + ((trans_left - 1) >> 1);						\
		assert((TN) < (CSD)->max_tn_warn);									\
		assert((CSD)->max_tn_warn <= (CSD)->max_tn);								\
	}														\
} MBEND

#define	INCREMENT_CURR_TN(CSD)							\
MBSTART {									\
	assert((CSD)->trans_hist.curr_tn < (CSD)->max_tn_warn);			\
	assert((CSD)->max_tn_warn <= (CSD)->max_tn);				\
	(CSD)->trans_hist.curr_tn++;						\
	assert((CSD)->trans_hist.curr_tn == (CSD)->trans_hist.early_tn);	\
} MBEND

#define SET_TN_WARN(CSD, ret_warn_tn)									\
MBSTART {												\
	trans_num	headroom;									\
													\
	headroom = (gtm_uint64_t)(GDSV4 == (CSD)->desired_db_format ? TN_HEADROOM_V4 : TN_HEADROOM_V6);	\
	headroom *= HEADROOM_FACTOR;									\
	(ret_warn_tn) = (CSD)->trans_hist.curr_tn;							\
	if ((headroom < (CSD)->max_tn) && ((ret_warn_tn) < ((CSD)->max_tn - headroom)))			\
		(ret_warn_tn) = (CSD)->max_tn - headroom;						\
	assert((CSD)->trans_hist.curr_tn <= (ret_warn_tn));						\
	assert((ret_warn_tn) <= (CSD)->max_tn);								\
} MBEND

#define HIST_TERMINATOR		0
#define HIST_SIZE(h)		( (SIZEOF(int4) * 2) + (SIZEOF(srch_blk_status) * ((h).depth + 1)) )

/* Start of lock space in a bg file, therefore also doubles as overhead size for header, bt and wc queues F = # of wc blocks */
#define LOCK_BLOCK(X) 		(DIVIDE_ROUND_UP(SIZEOF_FILE_HDR(X) + BT_SIZE(X), DISK_BLOCK_SIZE))
#define LOCK_BLOCK_SIZE(X)	(DIVIDE_ROUND_UP(SIZEOF_FILE_HDR(X) + BT_SIZE(X), OS_PAGE_SIZE))
#define	LOCK_SPACE_SIZE(X)	(ROUND_UP2(((sgmnt_data_ptr_t)X)->lock_space_size, OS_PAGE_SIZE))
/* In case of an encrypted database, we maintain both encrypted and decrypted versions of the block in shared memory
 * in parallel arrays of global buffers hence the doubling calculation below. Although this doubles the shared memory
 * size requirements for encrypted databases (when compared to the same unencrypted database), it helps in other ways.
 * By ensuring that this encrypted global buffer array contents are identical to the encrypted on-disk block contents
 * of database blocks at all times, we can avoid allocating process private memory to store encrypted before-images
 * (to write to a journal file). Instead processes can use the encrypted global buffer directly for this purpose.
 * In user environments where process-private memory is very costly compared to database shared memory (e.g. where
 * 1000s of GT.M processes run against the same database) the above approach is expected to use less total memory.
 */
#define CACHE_CONTROL_SIZE(X)												\
	(ROUND_UP((ROUND_UP((X->bt_buckets + X->n_bts) * SIZEOF(cache_rec) + SIZEOF(cache_que_heads), OS_PAGE_SIZE)	\
		+ ((gtm_uint64_t)X->n_bts * X->blk_size * (USES_ENCRYPTION(X->is_encrypted) ? 2 : 1))), OS_PAGE_SIZE))

OS_PAGE_SIZE_DECLARE

/* structure to identify a given system wide shared section to be ours (replic section) */
typedef struct
{
	unsigned char	label[GDS_LABEL_SZ];
	char		pool_type;
	char		now_running[MAX_REL_NAME];
	int4		repl_pool_key_filler;		/* makes sure the size of the structure is a multiple of 8 */
	char		instfilename[MAX_FN_LEN + 1];	/* Identify which instance file this shared pool corresponds to */
} replpool_identifier;

#if defined(__osf__) && defined(__alpha)
# pragma pointer_size(save)
# pragma pointer_size(long)
#endif

typedef replpool_identifier 	*replpool_id_ptr_t;

#if defined(__osf__) && defined(__alpha)
# pragma pointer_size(restore)
#endif

/* Macro to increment the count of processes that are doing two phase commit.
 * This is invoked just BEFORE starting phase1 of the commit.
 */
#define	INCR_WCS_PHASE2_COMMIT_PIDCNT(csa, cnl)				\
MBSTART {								\
	assert(!csa->wcs_pidcnt_incremented);				\
	INCR_CNT(&cnl->wcs_phase2_commit_pidcnt, &cnl->wc_var_lock);	\
	csa->wcs_pidcnt_incremented = TRUE;				\
} MBEND

/* Macro to decrement the count of processes that are doing two phase commit.
 * This is invoked just AFTER finishing phase2 of the commit.
 */
#define	DECR_WCS_PHASE2_COMMIT_PIDCNT(csa, cnl)				\
MBSTART {								\
	assert(csa->wcs_pidcnt_incremented);				\
	csa->wcs_pidcnt_incremented = FALSE;				\
	DECR_CNT(&cnl->wcs_phase2_commit_pidcnt, &cnl->wc_var_lock);	\
} MBEND

/* Insert the process_id into the list of process ids actively doing a kill */
#define INSERT_KIP_PID(local_csa)						\
MBSTART {									\
	int		idx;							\
	uint4		pid;							\
	uint4		*kip_pid_arr_ptr;					\
	GBLREF uint4	process_id;						\
										\
	kip_pid_arr_ptr = local_csa->nl->kip_pid_array;				\
	assert(local_csa->now_crit);						\
	for (idx = 0; idx < MAX_KIP_PID_SLOTS; idx++)				\
	{									\
		pid = kip_pid_arr_ptr[idx];					\
		if ((0 == pid) || (process_id == pid))				\
		{								\
			kip_pid_arr_ptr[idx] = process_id;			\
			break;							\
		}								\
	}									\
} MBEND
/* Remove the process_id from the list of process ids actively doing a kill */
#define REMOVE_KIP_PID(local_csa)						\
MBSTART {									\
	int		idx;							\
	uint4		*kip_pid_arr_ptr;					\
	GBLREF uint4	process_id;						\
										\
	kip_pid_arr_ptr = local_csa->nl->kip_pid_array;				\
	for (idx = 0; idx < MAX_KIP_PID_SLOTS; idx++)				\
	{									\
		if (process_id == kip_pid_arr_ptr[idx])				\
		{								\
			kip_pid_arr_ptr[idx] = 0;				\
			break;							\
		}								\
	}									\
} MBEND
/* Insert the process_id into the list of process ids with active wcs_timers
 * Note: Unreliable - For Diagnostic Purposes Only
 */
#define INSERT_WT_PID(local_csa)												\
MBSTART {															\
	int		idx;													\
	uint4		*wt_pid_arr_ptr;											\
	GBLREF uint4	process_id;												\
																\
	wt_pid_arr_ptr = local_csa->nl->wt_pid_array;										\
	for (idx = 0; idx < MAX_WT_PID_SLOTS; idx++)										\
	{															\
		/* Unreliable, as there is a race for the empty slot. */							\
		if (0 == wt_pid_arr_ptr[idx])											\
			wt_pid_arr_ptr[idx] = process_id;									\
		if (process_id == wt_pid_arr_ptr[idx])										\
			break;													\
	}															\
} MBEND
/* Remove the process_id from the list of process ids with active wcs_timers
 * Note: Unreliable - For Diagnostic Purposes Only
 */
#define REMOVE_WT_PID(local_csa)						\
MBSTART {									\
	int		idx;							\
	uint4		*wt_pid_arr_ptr;					\
	GBLREF uint4	process_id;						\
										\
	wt_pid_arr_ptr = local_csa->nl->wt_pid_array;				\
	for (idx = 0; idx < MAX_WT_PID_SLOTS; idx++)				\
	{									\
		if (process_id == wt_pid_arr_ptr[idx])				\
		{								\
			wt_pid_arr_ptr[idx] = 0;				\
			break;							\
		}								\
	}									\
} MBEND

#define DECR_KIP(CSD, CSA, KIP_CSA)						\
MBSTART {									\
	sgmnt_data_ptr_t	local_csd;					\
	sgmnt_addrs		*local_csa;					\
										\
	/* Instead of using CSA and CSD directly in DECR_CNT, assign it to 	\
	 * local variables as the caller can potentially pass the global 	\
	 * kip_csa as the second argument(which also happens to be the 		\
	 * the third argument which will be reset to NULL below) thereby	\
	 * leading to SEG faults in the calls to DECR_CNT. Similar 		\
	 * modifications are in INCR_KIP and their CAREFUL counterparts */	\
	local_csd = CSD;							\
	local_csa = CSA;							\
	assert(NULL != KIP_CSA);						\
	KIP_CSA = NULL;								\
	DECR_CNT(&local_csd->kill_in_prog, &local_csa->nl->wc_var_lock);	\
	REMOVE_KIP_PID(local_csa);						\
} MBEND
#define INCR_KIP(CSD, CSA, KIP_CSA)						\
MBSTART {									\
	sgmnt_data_ptr_t	local_csd;					\
	sgmnt_addrs		*local_csa;					\
										\
	local_csd = CSD;							\
	local_csa = CSA;							\
	assert(NULL == KIP_CSA);						\
	INCR_CNT(&local_csd->kill_in_prog, &local_csa->nl->wc_var_lock);	\
	INSERT_KIP_PID(local_csa);						\
	KIP_CSA = CSA;								\
} MBEND

/* Since abandoned_kills counter is only incremented in secshr_db_clnup it does not have its equivalent DECR_ABANDONED_KILLS */
#define INCR_ABANDONED_KILLS(CSD, CSA)	INCR_CNT(&CSD->abandoned_kills, &CSA->nl->wc_var_lock)

#define INCR_INHIBIT_KILLS(CNL)		INCR_CNT(&CNL->inhibit_kills, &CNL->wc_var_lock)

#define DECR_INHIBIT_KILLS(CNL)						\
MBSTART {								\
	if (0 < CNL->inhibit_kills)					\
		DECR_CNT(&CNL->inhibit_kills, &CNL->wc_var_lock);	\
} MBEND

/* Unless the pipeline architecture of the machine precludes it, there is a chance for another process to slip in between the IF and
 * the decrement, but this macro would only be used in relatively unlikely circumstances.
 */
#define CAREFUL_DECR_CNT(CNT,LATCH)			\
MBSTART {						\
	if (0 < CNT)					\
		DECR_CNT(&CNT, &LATCH);			\
} MBEND

/* Commands like MUPIP BACKUP, MUPIP INTEG -REG or MUPIP FREEZE wait for kills-in-prog flag to become zero.
 * While these process wait for ongoing block-freeing KILLs (or reorg actions that free up blocks) to complete,
 * new block-freeing KILLs (or reorg actions that free up blocks) are deferred using inhibit_kills counter.
 * New block-freeing KILLs/REORG will wait for a maximum period of 1 minute until inhibit_kills counter is 0.
 * In case of timeout, they will proceed after resetting the inhibit_kills to 0. The reset is done in case
 * the inhibit_kills was orphaned (i.e. the process that set it got killed before it got a chance to reset).
 */
#define	WAIT_ON_INHIBIT_KILLS(CNL, MAXKILLINHIBITWAIT)				\
MBSTART {									\
	int4			sleep_counter;					\
										\
	GBLREF	boolean_t	need_kip_incr;					\
	GBLREF	uint4		dollar_tlevel;					\
										\
	assert(dollar_tlevel || need_kip_incr);					\
	for (sleep_counter = 1; (0 < CNL->inhibit_kills); ++sleep_counter)	\
	{									\
		if (MAXKILLINHIBITWAIT <= sleep_counter)			\
		{								\
			CNL->inhibit_kills = 0;					\
			SHM_WRITE_MEMORY_BARRIER;				\
			break;							\
		}								\
		wcs_sleep(sleep_counter);					\
	}									\
} MBEND

/* Wait for a region freeze to be turned off. Note that we don't hold CRIT at this point. Ideally we would have
 * READ memory barriers between each iterations of sleep to try and get the latest value of the "freeze" field from
 * the concurrently updated database shared memory. But since region-freeze is a perceivably rare event, we choose
 * not to do the memory barriers. The consequence of this decision is that it might take more iterations for us to
 * see updates to the "freeze" field than it would have if we did the memory barrier each iteration. But since we
 * don't hold crit at this point AND since freeze is a rare event, we don't mind the extra wait.
 */
#define MAXHARDCRITS		31

#define	WAIT_FOR_REGION_TO_UNFREEZE(CSA)		\
MBSTART {						\
	int	lcnt1;					\
							\
	assert(!CSA->now_crit);				\
	for (lcnt1 = 1; ; lcnt1++)			\
	{						\
		if (!FROZEN_HARD(CSA))			\
			break;				\
		if (MAXHARDCRITS < lcnt1)       	\
			wcs_backoff(lcnt1);     	\
	}						\
} MBEND

#define	WAIT_FOR_REGION_TO_UNCHILL(CSA, CSD)										\
MBSTART {														\
	int			lcnt1;											\
	boolean_t		crit_stuck = FALSE;									\
															\
	assert((CSA)->hdr == (CSD));											\
	for (lcnt1 = 1; ; lcnt1++)											\
	{														\
		if (!FROZEN_CHILLED(CSA))										\
			break;												\
		if (CHILLED_AUTORELEASE(CSA))										\
		{													\
			DO_CHILLED_AUTORELEASE(CSA, CSD);								\
			break;												\
		}													\
		else if ((CSA)->now_crit && !crit_stuck)								\
		{	/* Assert that we do not hold the ftok or access semaphore as that can cause deadlocks		\
			 * while we have crit and wait below for freeze to be lifted, since the process to lift		\
			 * the freeze off (MUPIP FREEZE -OFF) would need access to the ftok semaphore.			\
			 */												\
			DEBUG_ONLY(unix_db_info	*udi);									\
															\
			DEBUG_ONLY(udi = (unix_db_info *)CSA;)								\
			assert(CSA == &udi->s_addrs);									\
			assert(!udi->grabbed_ftok_sem);									\
			assert(!udi->grabbed_access_sem);								\
			send_msg_csa(CSA_ARG(CSA) VARLSTCNT(4) ERR_OFRZCRITSTUCK, 2, REG_LEN_STR((CSA)->region));	\
			crit_stuck = TRUE;										\
		}													\
		if (MAXHARDCRITS < lcnt1)       									\
			wcs_backoff(lcnt1);     									\
	}														\
	if (crit_stuck)													\
		send_msg_csa(CSA_ARG(CSA) VARLSTCNT(4) ERR_OFRZCRITREL, 2, REG_LEN_STR((CSA)->region));			\
} MBEND

/* Since this macro is called from "t_retry", we need to ensure encryption cycles are synced as part of
 * the grab_crit, hence the "grab_crit_encr_cycle_sync" usage. Other callers of this macro like "mupip_extend"
 * don't need that functionality but it does not hurt them so we leave it at that instead of forking this
 * macro into two versions (one using "grab_crit" and another using "grab_crit_encr_cycle_sync").
 */
#define	GRAB_UNFROZEN_CRIT(reg, csa)					\
MBSTART {								\
	int	lcnt;							\
									\
	assert(&FILE_INFO(reg)->s_addrs == csa);			\
	assert(csa->now_crit);						\
	for (lcnt = 0; ; lcnt++)					\
	{								\
		if (!FROZEN_HARD(csa))					\
			break;						\
		rel_crit(reg);						\
		WAIT_FOR_REGION_TO_UNFREEZE(csa);			\
		grab_crit_encr_cycle_sync(reg);				\
	}								\
	assert(!FROZEN_HARD(csa) && csa->now_crit);			\
} MBEND

/* remove "csa" from list of open regions (cs_addrs_list) */
#define	REMOVE_CSA_FROM_CSADDRSLIST(CSA)						\
MBSTART {										\
	GBLREF	sgmnt_addrs	*cs_addrs_list;						\
											\
	sgmnt_addrs	*tmpcsa, *prevcsa;						\
											\
	assert(NULL != CSA);								\
	assert(NULL == CSA->nl);							\
	prevcsa = NULL;									\
	for (tmpcsa = cs_addrs_list; NULL != tmpcsa; tmpcsa = tmpcsa->next_csa)		\
	{										\
		if (CSA == tmpcsa)							\
			break;								\
		prevcsa = tmpcsa;							\
	}										\
	/* tmpcsa could not be equal to CSA in case CSA was never added to this list	\
	 * (possible in case of errors during gvcst_init). In dbg, the only case we 	\
	 * know of this is if an external signal causes exit processing before db_init	\
	 * completes. Assert accordingly.						\
	 */										\
	assert((tmpcsa == CSA) || process_exiting);					\
	if (tmpcsa == CSA)								\
	{										\
		if (NULL != prevcsa)							\
			prevcsa->next_csa = CSA->next_csa;				\
		else									\
			cs_addrs_list = CSA->next_csa;					\
	}										\
} MBEND

#define RESET_SHMID_CTIME(X)			\
MBSTART {					\
	(X)->shmid = INVALID_SHMID;		\
	(X)->gt_shm_ctime.ctime = 0;		\
} MBEND

#define RESET_SEMID_CTIME(X)			\
MBSTART {					\
	(X)->semid = INVALID_SEMID;		\
	(X)->gt_sem_ctime.ctime = 0;		\
} MBEND

#define RESET_IPC_FIELDS(X)		\
MBSTART {				\
	RESET_SHMID_CTIME(X);		\
	RESET_SEMID_CTIME(X);		\
} MBEND

GBLREF	boolean_t	multi_thread_in_use;		/* TRUE => threads are in use. FALSE => not in use */

/* #GTM_THREAD_SAFE : The below macro (DB_FSYNC) is thread-safe */
#define DB_FSYNC(reg, udi, csa, db_fsync_in_prog, save_errno)						\
MBSTART {												\
	int	rc;											\
													\
	BG_TRACE_PRO_ANY(csa, n_db_fsyncs);								\
	if (csa->now_crit)										\
		BG_TRACE_PRO_ANY(csa, n_db_fsyncs_in_crit);						\
	/* If inside thread, do not touch global variable "db_fsync_in_prog" due to concurrency issues.	\
	 * Besides, no need to maintain this variable inside thread since SIGALRMs are blocked and	\
	 * this is primarily used by "wcs_clean_dbsync" (the idle epoch timer code) anyways.		\
	 */												\
	if (!multi_thread_in_use)									\
		db_fsync_in_prog++;									\
	save_errno = 0;											\
	GTM_DB_FSYNC(csa, udi->fd, rc);									\
	if (-1 == rc)											\
		save_errno = errno;									\
	if (!multi_thread_in_use)									\
		db_fsync_in_prog--;									\
	assert(0 <= db_fsync_in_prog);									\
} MBEND

#define STANDALONE(x) mu_rndwn_file(x, TRUE)
#define DBFILOP_FAIL_MSG(status, msg)	gtm_putmsg_csa(CSA_ARG(REG2CSA(gv_cur_region)) VARLSTCNT(5) msg, 2,			\
							DB_LEN_STR(gv_cur_region), status);

#define CR_NOT_ALIGNED(cr, cr_base)		(!IS_PTR_ALIGNED((cr), (cr_base), SIZEOF(cache_rec)))
#define CR_NOT_IN_RANGE(cr, cr_lo, cr_hi)	(!IS_PTR_IN_RANGE((cr), (cr_lo), (cr_hi)))

/* Examine that cr->buffaddr is indeed what it should be. If not, this macro fixes its value by
 * recomputing from the cache_array.
 * NOTE: We rely on bt_buckets, n_bts and blk_size fields of file header being correct/not corrupt */
#define CR_BUFFER_CHECK(reg, csa, csd, cr)							\
MBSTART {											\
	cache_rec_ptr_t		cr_lo, cr_hi;							\
												\
	cr_lo = (cache_rec_ptr_t)csa->acc_meth.bg.cache_state->cache_array + csd->bt_buckets;	\
	cr_hi = cr_lo + csd->n_bts;								\
	CR_BUFFER_CHECK1(reg, csa, csd, cr, cr_lo, cr_hi);					\
} MBEND

/* A more efficient macro than CR_BUFFER_CHECK when we have cr_lo and cr_hi already available */
#define CR_BUFFER_CHECK1(reg, csa, csd, cr, cr_lo, cr_hi)					\
MBSTART {											\
	INTPTR_T bp, bp_lo, bp_top, cr_top;							\
												\
	cr_top = GDS_ANY_ABS2REL(csa, cr_hi);							\
	bp_lo = ROUND_UP(cr_top, OS_PAGE_SIZE);							\
	bp = bp_lo + ((cr) - (cr_lo)) * csd->blk_size;						\
	if (bp != cr->buffaddr)									\
	{											\
		send_msg_csa(CSA_ARG(csa) VARLSTCNT(13) ERR_DBCRERR, 11, DB_LEN_STR(reg),	\
				cr, cr->blk, RTS_ERROR_TEXT("cr->buffaddr"),			\
				cr->buffaddr, bp, CALLFROM);					\
		cr->buffaddr = bp;								\
	}											\
	DEBUG_ONLY(bp_top = bp_lo + (gtm_uint64_t)csd->n_bts * csd->blk_size;)			\
	assert(IS_PTR_IN_RANGE(bp, bp_lo, bp_top) && IS_PTR_ALIGNED(bp, bp_lo, csd->blk_size));	\
} MBEND

#define FILE_CNTL_INIT_IF_NULL(SEG)								\
MBSTART {											\
	file_control	*lcl_fc;								\
	sgmnt_addrs	*csa;									\
												\
	lcl_fc = SEG->file_cntl;								\
	if (NULL == lcl_fc)									\
	{											\
		MALLOC_INIT(lcl_fc, SIZEOF(file_control));					\
		SEG->file_cntl = lcl_fc;							\
	}											\
	if (NULL == lcl_fc->file_info)								\
	{											\
		MALLOC_INIT(lcl_fc->file_info, SIZEOF(unix_db_info));				\
		SEG->file_cntl->file_info = lcl_fc->file_info;					\
		csa = &((unix_db_info *)(lcl_fc->file_info))->s_addrs;				\
		csa->gvstats_rec_p = &csa->gvstats_rec;						\
	}											\
} MBEND

#define FILE_CNTL_INIT(SEG)						\
MBSTART {								\
	file_control	*lcl_fc;					\
	sgmnt_addrs	*csa;						\
									\
	MALLOC_INIT(lcl_fc, SIZEOF(file_control));			\
	MALLOC_INIT(lcl_fc->file_info, SIZEOF(unix_db_info));		\
	SEG->file_cntl = lcl_fc;					\
	csa = &((unix_db_info *)(lcl_fc->file_info))->s_addrs;		\
	csa->gvstats_rec_p = &csa->gvstats_rec;				\
} MBEND

#define	FILE_CNTL_FREE(SEG)						\
MBSTART {								\
	file_control	*lcl_fc;					\
	unix_db_info	*udi;						\
									\
	lcl_fc = SEG->file_cntl;					\
	if (NULL != lcl_fc)						\
	{								\
		udi = (unix_db_info *)lcl_fc->file_info;		\
		if (NULL != udi)					\
		{							\
			free(udi);					\
			lcl_fc->file_info = NULL;			\
		}							\
		free(lcl_fc);						\
		SEG->file_cntl = NULL;					\
	}								\
} MBEND

#define	IS_DOLLAR_INCREMENT			((is_dollar_incr) && (ERR_GVPUTFAIL == t_err))

#define AVG_BLKS_PER_100_GBL		200
#define PRE_READ_TRIGGER_FACTOR		50
#define UPD_RESERVED_AREA		50
#define UPD_WRITER_TRIGGER_FACTOR	33

#define ONLY_SS_BEFORE_IMAGES(CSA) (CSA->snapshot_in_prog && !CSA->backup_in_prog && !(JNL_ENABLED(CSA) && CSA->jnl_before_image))

#define SET_SNAPSHOTS_IN_PROG(X)	((X)->snapshot_in_prog = TRUE)
#define CLEAR_SNAPSHOTS_IN_PROG(X)	((X)->snapshot_in_prog = FALSE)
# define SNAPSHOTS_IN_PROG(X)	((X)->snapshot_in_prog)
/* Creates a new snapshot context. Called by GT.M (or utilities like update process, MUPIP LOAD which uses
 * GT.M runtime. As a side effect sets csa->snapshot_in_prog to TRUE if the context creation went fine.
 */
# define SS_INIT_IF_NEEDED(CSA, CNL)											\
MBSTART {														\
	int			ss_shmcycle;										\
	boolean_t		status;											\
	snapshot_context_ptr_t	lcl_ss_ctx;										\
															\
	lcl_ss_ctx = SS_CTX_CAST(CSA->ss_ctx);										\
	assert(NULL != lcl_ss_ctx);											\
	CNL->fastinteg_in_prog ? SET_FAST_INTEG(lcl_ss_ctx) : SET_NORM_INTEG(lcl_ss_ctx);				\
	ss_shmcycle = CNL->ss_shmcycle;											\
	SET_SNAPSHOTS_IN_PROG(CSA);											\
	assert(lcl_ss_ctx->ss_shmcycle <= ss_shmcycle);									\
	if (lcl_ss_ctx->ss_shmcycle != ss_shmcycle)									\
	{	/* Process' view of snapshot is stale. Create/Update snapshot context */				\
		status = ss_create_context(lcl_ss_ctx, ss_shmcycle);							\
		if (!status)												\
		{	/* snapshot context creation failed. Reset private copy of snapshot_in_prog so that we don't	\
			 * read the before images in t_end or op_tcommit */						\
			CLEAR_SNAPSHOTS_IN_PROG(CSA);									\
		}													\
		assert(!status || (SNAPSHOT_INIT_DONE == lcl_ss_ctx->cur_state));					\
		assert(status || (SHADOW_FIL_OPEN_FAIL == lcl_ss_ctx->cur_state)					\
			|| (SNAPSHOT_SHM_ATTACH_FAIL  == lcl_ss_ctx->cur_state)						\
			|| (SNAPSHOT_NOT_INITED == lcl_ss_ctx->cur_state));						\
	} else if ((SHADOW_FIL_OPEN_FAIL == lcl_ss_ctx->cur_state) 							\
			|| (SNAPSHOT_SHM_ATTACH_FAIL == lcl_ss_ctx->cur_state))						\
	{	/* Previous attempt at snapshot context creation failed (say, snapshot file open failed) and the error	\
		 * has been reported in the shared memory. However, the snapshot is not yet complete. So, set 		\
		 * snapshot_in_prog to FALSE since the ongoing snapshot is not valid (as indicated by us in the prior	\
		 * transaction/retry inside crit) 									\
		 * Note that we will be doing this 'if' check unconditionally until MUPIP INTEG detects the error in 	\
		 * shared memory which can be avoided by making GT.M itself set CNL->snapshot_in_prog to FALSE when it	\
		 * detects inside crit that snapshot initialization failed for this process and hence the ongoing	\
		 * snapshot is no longer valid. This way we don't wait for MUPIP INTEG to detect and terminate the 	\
		 * snapshots												\
		 */													\
		CLEAR_SNAPSHOTS_IN_PROG(CSA);										\
	}														\
} MBEND

#ifdef DEBUG
# define DBG_ENSURE_SNAPSHOT_GOOD_TO_GO(LCL_SS_CTX, CNL)				\
MBSTART {										\
	shm_snapshot_ptr_t		ss_shm_ptr;					\
											\
	assert(SNAPSHOTS_IN_PROG(CNL));							\
	assert(NULL != LCL_SS_CTX);							\
	ss_shm_ptr = LCL_SS_CTX->ss_shm_ptr;						\
	assert(NULL != ss_shm_ptr);							\
	assert(SNAPSHOT_INIT_DONE == LCL_SS_CTX->cur_state);				\
	assert(0 == LCL_SS_CTX->failure_errno);						\
	assert((-1 != CNL->ss_shmid) && 						\
		(LCL_SS_CTX->attach_shmid == CNL->ss_shmid));				\
	assert(NULL != LCL_SS_CTX->start_shmaddr);					\
	assert(0 == STRCMP(LCL_SS_CTX->shadow_file, ss_shm_ptr->ss_info.shadow_file));	\
	assert(-1 != LCL_SS_CTX->shdw_fd);						\
} MBEND
#else
# define DBG_ENSURE_SNAPSHOT_GOOD_TO_GO(LCL_SS_CTX, CNL)
#endif
/* Destroy an existing snapshot. Called by GT.M (or utilities like update process, MUPIP LOAD which uses
 * GT.M runtime. Assumes that csa->snapshot_in_prog is TRUE and as a side effect sets csa->snapshot_in_prog
 * to FALSE if the context is destroyed
 */
# define SS_RELEASE_IF_NEEDED(CSA, CNL)							\
MBSTART {										\
	int			ss_shmcycle;						\
	snapshot_context_ptr_t	lcl_ss_ctx;						\
											\
	lcl_ss_ctx = SS_CTX_CAST(CSA->ss_ctx);						\
	assert(SNAPSHOTS_IN_PROG(CSA) && (NULL != lcl_ss_ctx));				\
	ss_shmcycle = CNL->ss_shmcycle;							\
	if (!SNAPSHOTS_IN_PROG(cnl) || (lcl_ss_ctx->ss_shmcycle != ss_shmcycle))	\
	{										\
		ss_destroy_context(lcl_ss_ctx);						\
		CLEAR_SNAPSHOTS_IN_PROG(CSA);						\
	}										\
} MBEND

/* No need to write before-image in case the block is FREE. In case the database had never been fully upgraded from V4 to V5 format
 * (after the MUPIP UPGRADE), all RECYCLED blocks can basically be considered FREE (i.e. no need to write before-images since
 * backward journal recovery will never be expected to take the database to a point BEFORE the mupip upgrade).
 * Logic to check if before image of a given block has to be read or not are slightly complicated if snapshots are present
 * For snapshots, we might want to read the before images of FREE blocks. Also, if the block that we are reading
 * is already before imaged by some other GT.M process then we do not needed to read the before image of such a block. But, such
 * a condition is applicable ONLY if snapshots alone are in progress as we might want the same block for BACKUP if it is in
 * progress.
 * Note: The below condition, to before image FREE blocks, is needed only if INTEG is the snapshot initiator. When we add
 * bitmasks or some alternate mechanism to optionalize the before image'ing of FREE blocks, this condition must be tweaked
 * accordingly. For now, INTEG is the only snapshot initiator.
 */
# define BEFORE_IMAGE_NEEDED(read_before_image, CS, csa, csd, blk_no, retval)				 		\
MBSTART {													 	\
	retval = (read_before_image && csd->db_got_to_v5_once);								\
	retval = retval && (!WAS_FREE(CS->blk_prior_state) || SNAPSHOTS_IN_PROG(csa));				\
	retval = retval && (!ONLY_SS_BEFORE_IMAGES(csa) || !ss_chk_shdw_bitmap(csa, SS_CTX_CAST(csa->ss_ctx), blk_no));\
} MBEND

# define CHK_AND_UPDATE_SNAPSHOT_STATE_IF_NEEDED(CSA, CNL, SS_NEED_TO_RESTART)							\
MBSTART {															\
	GBLREF	uint4		process_id;											\
																\
	uint4			lcl_failure_errno;										\
	ss_proc_status		cur_state;											\
	shm_snapshot_ptr_t	ss_shm_ptr;											\
	snapshot_context_ptr_t	lcl_ss_ctx;											\
	boolean_t		csa_snapshot_in_prog, cnl_snapshot_in_prog;							\
																\
	assert(CSA->now_crit);													\
	csa_snapshot_in_prog = SNAPSHOTS_IN_PROG(CSA);										\
	cnl_snapshot_in_prog = SNAPSHOTS_IN_PROG(CNL);										\
	if (csa_snapshot_in_prog || cnl_snapshot_in_prog)									\
	{															\
		lcl_ss_ctx = SS_CTX_CAST(CSA->ss_ctx);										\
		ss_shm_ptr = (shm_snapshot_ptr_t)(SS_GETSTARTPTR(CSA));								\
		assert(lcl_ss_ctx->ss_shmcycle <= CNL->ss_shmcycle);								\
		if (!cnl_snapshot_in_prog || ss_shm_ptr->failure_errno)								\
		{	/* No on going snapshots or on going snapshot is invalid. Even if we encountered error during snapshot	\
			 * context creation outside crit, we ignore it as the snapshot is no more active/valid. 		\
			 */													\
			CLEAR_SNAPSHOTS_IN_PROG(CSA);										\
		} else if (lcl_ss_ctx->ss_shmcycle == CNL->ss_shmcycle)								\
		{	/* Neither new snapshots started nor existing ones completed. However, it's possible that we might have \
			 * encountered error during snapshot context creation outside crit. If the values noted outside crit 	\
			 * matches with the global values, then the error is genuine. If not, then we might have done operations\
			 * (shm attach and file open) when things in the shared memory were in flux in which case we need to	\
			 * restart												\
			 */													\
			lcl_failure_errno = lcl_ss_ctx->failure_errno;								\
			assert(!ss_shm_ptr->failure_errno);									\
			SS_NEED_TO_RESTART = FALSE;										\
			cur_state = lcl_ss_ctx->cur_state;									\
			switch(cur_state)											\
			{													\
				case SNAPSHOT_INIT_DONE:									\
					/* Most common case. Ensure the local values of snapshot context matches with the 	\
					 * values stored in shared memory */							\
					assert(csa_snapshot_in_prog);								\
					DBG_ENSURE_SNAPSHOT_GOOD_TO_GO(lcl_ss_ctx, CNL);					\
					break;											\
				case SNAPSHOT_SHM_ATTACH_FAIL:									\
					assert(0 != lcl_failure_errno);								\
					assert(FALSE == SNAPSHOTS_IN_PROG(CSA));						\
					if (lcl_ss_ctx->nl_shmid == CNL->ss_shmid)						\
					{	/* Error encountered outside crit is genuine. Indicate MUPIP INTEG that the 	\
						 * snapshot is no more valid 							\
						 */										\
						send_msg_csa(CSA_ARG(CSA) VARLSTCNT(4) ERR_SSATTACHSHM, 1,			\
								lcl_ss_ctx->nl_shmid, lcl_failure_errno);			\
						ss_shm_ptr->failure_errno = lcl_failure_errno;					\
						ss_shm_ptr->failed_pid = process_id;						\
					} else /* snapshot context creation done while things were in flux */			\
						SS_NEED_TO_RESTART = TRUE;							\
					break;											\
				case SHADOW_FIL_OPEN_FAIL:									\
					assert(0 != lcl_failure_errno);								\
					assert(FALSE == SNAPSHOTS_IN_PROG(CSA));						\
					if (0 == STRCMP(lcl_ss_ctx->shadow_file, ss_shm_ptr->ss_info.shadow_file))		\
					{	/* Error encountered outside crit is genuine. Indicate MUPIP INTEG that the 	\
						 * snapshot is no more valid							\
						 */										\
						send_msg_csa(CSA_ARG(CSA) VARLSTCNT(7) ERR_SSFILOPERR, 4, LEN_AND_LIT("open"), 	\
							LEN_AND_STR(lcl_ss_ctx->shadow_file), lcl_failure_errno);		\
						ss_shm_ptr->failure_errno = lcl_failure_errno;					\
						ss_shm_ptr->failed_pid = process_id;						\
					} else	/* snapshot context creation done while things were in flux */			\
						SS_NEED_TO_RESTART = TRUE;							\
					break;											\
				default:											\
					assert(FALSE);										\
			}													\
		} else /* A new snapshot has started after we grabbed crit in t_end. Need to restart */				\
			SS_NEED_TO_RESTART = TRUE;										\
	}															\
} MBEND

# define WRITE_SNAPSHOT_BLOCK(csa, cr, mm_blk_ptr, blkid, lcl_ss_ctx)			\
MBSTART {										\
	assert(NULL != lcl_ss_ctx);							\
	/* write this block to the snapshot shadow file only if this was not already	\
	 * before imaged. If error happens while writing to the snapshot file, then	\
	 * ss_write_block will mark the appropriate error in the shared memory		\
	 * which INTEG will later query and report accordingly. So, just continue	\
	 * as if nothing happened. 							\
	 */										\
	if (!ss_chk_shdw_bitmap(csa, lcl_ss_ctx, blkid))				\
		if (!ss_write_block(csa, blkid, cr, mm_blk_ptr, lcl_ss_ctx))		\
			assert(FALSE);							\
} MBEND

/* Determine if the state of 'backup in progress' has changed since we grabbed crit in t_end.c/tp_tend.c */
#define CHK_AND_UPDATE_BKUP_STATE_IF_NEEDED(CNL, CSA, NEW_BKUP_STARTED)				\
MBSTART {											\
	if (CSA->backup_in_prog != (BACKUP_NOT_IN_PROGRESS != CNL->nbb))			\
	{											\
		if (!CSA->backup_in_prog)							\
			NEW_BKUP_STARTED = TRUE;						\
		CSA->backup_in_prog = !CSA->backup_in_prog;					\
	}											\
} MBEND

#define BLK_HDR_EMPTY(bp) ((0 == (bp)->bsiz) && (0 == (bp)->tn))

typedef enum
{
	REG_FREEZE_SUCCESS,
	REG_ALREADY_FROZEN,
	REG_HAS_KIP,
	REG_FLUSH_ERROR,
	REG_JNL_OPEN_ERROR,
	REG_JNL_SWITCH_ERROR
} freeze_status;

/* This structure holds state captured on entry into gvcst_redo_root_search which it restores on error or exit */
typedef struct redo_root_search_context_struct
{
	unsigned char	t_fail_hist[CDB_MAX_TRIES];
	unsigned int	t_tries;
	unsigned int	prev_t_tries;
	inctn_opcode_t	inctn_opcode;
	trans_num	start_tn;
	uint4		update_trans;
	uint4		t_err;
	boolean_t	hold_onto_crit;
	gv_key		currkey[DBKEYALLOC(MAX_KEY_SZ)];
	gv_key		*gv_currkey;
#	ifdef DEBUG
	unsigned char	t_fail_hist_dbg[T_FAIL_HIST_DBG_SIZE];
	unsigned int	t_tries_dbg;
#	endif
} redo_root_search_context;

#define SET_GV_CURRKEY_FROM_GVT(GVT)						\
MBSTART {									\
	mname_entry		*gvent;						\
	int			end;						\
										\
	GBLREF	gv_key		*gv_currkey;					\
	GBLREF	boolean_t	mu_reorg_process;				\
	GBLREF	gv_namehead	*reorg_gv_target;				\
										\
	assert((GVT != reorg_gv_target) || mu_reorg_process);			\
	gvent = &GVT->gvname;							\
	memcpy(gv_currkey->base, gvent->var_name.addr, gvent->var_name.len);	\
	end = gvent->var_name.len + 1;						\
	gv_currkey->end = end;							\
	gv_currkey->base[end - 1] = 0;						\
	gv_currkey->base[end] = 0;						\
} MBEND

#define SET_WANT_ROOT_SEARCH(CDB_STATUS, WANT_ROOT_SEARCH)								\
MBSTART {														\
	GBLREF	uint4			t_tries;									\
	GBLREF	gv_namehead		*gv_target;									\
															\
	if (cdb_sc_normal == cdb_status)										\
		cdb_status = LAST_RESTART_CODE;										\
	/* Below IF check is a special case where t_retry/tp_restart detects mismatched root cycles in 2nd to 3rd retry	\
	 * transition. In this case, the CDB_STATUS can be anything but we still need to redo the root search		\
	 */														\
	if ((CDB_STAGNATE == t_tries) && !gv_target->root)								\
		WANT_ROOT_SEARCH = TRUE;										\
	switch(CDB_STATUS)												\
	{														\
		case cdb_sc_onln_rlbk1:											\
		case cdb_sc_gvtrootmod:											\
		case cdb_sc_gvtrootmod2:										\
			WANT_ROOT_SEARCH = TRUE;									\
			break;												\
		case cdb_sc_onln_rlbk2:											\
			/* Database was taken back to a different logical state and we are an implicit TP		\
			 * transaction. Issue DBROLLEDBACK error that the application programmer can catch and do	\
			 * the necessary stuff.										\
			 */												\
			assert(gtm_trigger_depth == tstart_trigger_depth);						\
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_DBROLLEDBACK);					\
		default:												\
			break;												\
	}														\
} MBEND

#define REDO_ROOT_SEARCH_IF_NEEDED(WANT_ROOT_SEARCH, CDB_STATUS)							\
MBSTART {														\
	DEBUG_ONLY(GBLREF tp_frame	*tp_pointer;)									\
	GBLREF uint4			t_err;										\
	GBLREF uint4			update_trans;									\
	uint4				save_update_trans, save_t_err;							\
															\
	CDB_STATUS = cdb_sc_normal;											\
	if (WANT_ROOT_SEARCH)												\
	{	/* We are implicit transaction and one of two things has happened:					\
		 * 1. Online rollback updated the database but did NOT take us to a different logical state in which	\
		 *	case we've already done the restart, but the root is now reset to zero.				\
		 * 2. mu_swap_root concurrently moved root blocks. We've restarted and reset root to zero.		\
		 * In either case, do root search to establish the new root.						\
		 */													\
		NON_GTMTRIG_ONLY(assert(FALSE));									\
		assert(tp_pointer->implicit_tstart);									\
		assert(NULL != gv_target);										\
		save_t_err = t_err;											\
		save_update_trans = update_trans;									\
		GVCST_ROOT_SEARCH_DONOT_RESTART(CDB_STATUS);								\
		t_err = save_t_err;											\
		update_trans = save_update_trans;									\
		if (cdb_sc_normal == CDB_STATUS)									\
			WANT_ROOT_SEARCH = FALSE;									\
	}														\
} MBEND

#define ASSERT_BEGIN_OF_FRESH_TP_TRANS												\
MBSTART {															\
	GBLREF	sgm_info	*first_sgm_info;										\
	GBLREF	sgm_info	*sgm_info_ptr;											\
																\
	assert((NULL == first_sgm_info) || ((sgm_info_ptr == first_sgm_info) && (NULL == first_sgm_info->next_sgm_info)));	\
	assert((NULL == first_sgm_info) || (0 == sgm_info_ptr->num_of_blks));							\
} MBEND

#define GVCST_ROOT_SEARCH							\
MBSTART {	/* gvcst_root_search is invoked to establish the root block of a \
	 * given global (pointed to by gv_target). We always expect the root	\
	 * block of the directory tree to be 1 and so must never come here	\
	 * with gv_target pointing to directory tree. Assert that.		\
	 */									\
	GBLREF gv_namehead	*gv_target;					\
										\
	assert((NULL != gv_target) && (DIR_ROOT != gv_target->root));		\
	if (!gv_target->root)							\
		gvcst_root_search(FALSE);					\
} MBEND

/* Same as GVCST_ROOT_SEARCH, but tells gvcst_root_search NOT to restart but to return the status code back to the caller */
#define GVCST_ROOT_SEARCH_DONOT_RESTART(STATUS)					\
MBSTART {									\
	GBLREF gv_namehead	*gv_target;					\
										\
	assert((NULL != gv_target) && (DIR_ROOT != gv_target->root));		\
	STATUS = cdb_sc_normal;							\
	if (!gv_target->root)							\
		STATUS = gvcst_root_search(TRUE);				\
} MBEND

#define GVCST_ROOT_SEARCH_AND_PREP(est_first_pass)								\
MBSTART {													\
	/* Before beginning a spanning node (gvcst_xxx) or spanning region (gvcst_spr_xxx) try in a gvcst	\
	 * routine, make sure the root is established. If we've restarted issue DBROLLEDBACK appropriately.	\
	 */													\
	GBLREF	unsigned char		t_fail_hist[CDB_MAX_TRIES]; /* for LAST_RESTART_CODE */			\
	GBLREF	stack_frame		*frame_pointer;								\
	GBLREF	uint4			dollar_tlevel;								\
														\
	DCL_THREADGBL_ACCESS;											\
														\
	SETUP_THREADGBL_ACCESS;											\
	assert(dollar_tlevel);											\
	ASSERT_BEGIN_OF_FRESH_TP_TRANS;										\
	if (est_first_pass && (cdb_sc_onln_rlbk2 == LAST_RESTART_CODE))						\
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_DBROLLEDBACK);					\
	tp_set_sgm();												\
	GVCST_ROOT_SEARCH;											\
} MBEND

#define GV_BIND_NAME_ONLY(ADDR, TARG, GVNH_REG)		GVNH_REG = gv_bind_name(ADDR, TARG)

#define GV_BIND_NAME_AND_ROOT_SEARCH(ADDR, TARG, GVNH_REG)					\
MBSTART {											\
	enum db_acc_method	acc_meth;							\
												\
	GBLREF gd_region	*gv_cur_region;							\
	GBLREF gv_namehead	*gv_target;							\
												\
	GV_BIND_NAME_ONLY(ADDR, TARG, GVNH_REG);						\
	/* Skip GVCST_ROOT_SEARCH in case of spanning global.					\
	 * See comment at end of gv_bind_name.c for details.					\
	 */											\
	if (NULL == GVNH_REG->gvspan)								\
	{											\
		acc_meth = REG_ACC_METH(gv_cur_region);						\
		if (IS_ACC_METH_BG_OR_MM(acc_meth))						\
			GVCST_ROOT_SEARCH;							\
	} else if (!gv_target->root && gv_target->act_specified_in_gld && gv_target->act)	\
	{	/* gv_target->root is ZERO which means we have still not done a			\
		 * gvcst_root_search which implies "act_in_gvt" function has not yet been	\
		 * invoked. But this global has a non-zero act specified in gld. Invoke		\
		 * "act_in_gvt" now as this might be needed for transforming string subscripts	\
		 * as part of op_gvname etc.							\
		 */										\
		act_in_gvt(gv_target); /* note: this could issue COLLTYPVERSION error */	\
	}											\
} MBEND

#define	GET_REG_INDEX(ADDR, REG_START, REG, REG_INDEX)				\
MBSTART {									\
	assert((REG >= REG_START) && (REG < &ADDR->regions[ADDR->n_regions]));	\
	REG_INDEX = REG - REG_START;						\
} MBEND

#define	ACT_NOT_SPECIFIED	(MAXUINT4)

#define	DO_NCT_CHECK_FOR_SPANGBLS(GVT, GVNH_REG, REG)								\
MBSTART {													\
	if ((NULL != GVNH_REG->gvspan) && !GVT->nct_must_be_zero)						\
	{													\
		if (GVT->nct)											\
			rts_error_csa(CSA_ARG(GVT->gd_csa) VARLSTCNT(6) ERR_NCTCOLLSPGBL, 4, DB_LEN_STR(REG),	\
					GVT->gvname.var_name.len, GVT->gvname.var_name.addr);			\
		GVT->nct_must_be_zero = TRUE;									\
	}													\
} MBEND

/* Copy "act" from the .gld file (GBLNAME section) to the GVNH_REG structure and in turn the GVT structure */
#define	COPY_ACT_FROM_GLD_TO_GVNH_REG_AND_GVT(ADDR, GVT, GBL_SPANS_REG, GVNH_REG, REG)			\
MBSTART {												\
	gd_gblname	*gname;										\
													\
	if ((NULL != ADDR) && (((gd_addr *)ADDR)->n_gblnames))						\
	{	/* have some global names with collation characteristics. check if current global	\
		 * name is part of that list. If so initialize its collation properties.		\
		 */											\
		gname = gv_srch_gblname(ADDR, GVT->gvname.var_name.addr, GVT->gvname.var_name.len);	\
	} else												\
		gname = NULL;										\
	if (NULL != gname)										\
	{	/* Transfer global's collation characteristics into gvnh_reg.				\
		 * But before that check for error scenarios.						\
		 */											\
		GVNH_REG->act = gname->act;								\
		GVNH_REG->ver = gname->ver;								\
	} else if (GBL_SPANS_REG)									\
	{	/* This global spans multiple regions. And the user did not specify a collation for	\
		 * this global in the GBLNAME section of the gld. In this case force collation to 0	\
		 * for this global. Not doing so could cause any non-zero default collation properties	\
		 * in the spanned global db file header to result in different parts of this global	\
		 * exist with different collation representations in different .dat files creating	\
		 * an out-of-design situation since the collation property of a spanning global is used	\
		 * in op_gvname/op_gvextnam/op_gvnaked to come with the subscript representation even	\
		 * before determining which region a given subscripted key maps to.			\
		 */											\
		GVNH_REG->act = 0;									\
		GVNH_REG->ver = 0;									\
	} else												\
		GVNH_REG->act = ACT_NOT_SPECIFIED;							\
	COPY_ACT_FROM_GVNH_REG_TO_GVT(GVNH_REG, GVT, REG);						\
} MBEND

/* Copy "act" from the GVNH_REG structure to the GVT structure.
 * Currently the GLD (and in turn GVNH_REG) don't have a way to set "nct" for a gblname.
 * Therefore only "act" gets copied over currently. This macro needs to change if/when
 * "nct" support for gblname gets added to the gld.
 */
#define	COPY_ACT_FROM_GVNH_REG_TO_GVT(GVNH_REG, GVT, REG)						\
MBSTART {												\
	uint4	gldact;											\
													\
	DO_NCT_CHECK_FOR_SPANGBLS(GVT, GVNH_REG, REG);							\
	gldact = GVNH_REG->act;										\
	/* Exclude DSE from ERR_ACTCOLLMISMTCH errors (see gvcst_root_search.c comment for details) */	\
	if (((ACT_NOT_SPECIFIED != gldact) && (gldact != GVT->act) && !IS_DSE_IMAGE)			\
		&& ((GVT->root) || GVT->act_specified_in_gld))						\
	{	/* GVT->root case : Global already exists and GVT/GLD act do not match.			\
		 * GVT->act_specified_in_gld case :							\
		 *	Global directory defines one alternate collation sequence for global name	\
		 *	but gv_target already has a different (and non-zero) alternate collation	\
		 *	sequence defined (from another global directory's GBLNAME characteristics	\
		 *	or from the "Default Collation" field of the database file header.		\
		 * In either case, error out.								\
		 */											\
		rts_error_csa(CSA_ARG(cs_addrs) VARLSTCNT(8) ERR_ACTCOLLMISMTCH, 6,			\
				GVT->gvname.var_name.len, GVT->gvname.var_name.addr,			\
				gldact, DB_LEN_STR(REG), GVT->act);					\
	}												\
	if (!GVT->act_specified_in_gld && (ACT_NOT_SPECIFIED != gldact))				\
	{												\
		GVT->act_specified_in_gld = TRUE;							\
		GVT->act = gldact;									\
		GVT->ver = GVNH_REG->ver;								\
	}												\
	GVT->nct_must_be_zero = (NULL != GVNH_REG->gvspan);						\
} MBEND

/* This macro finishes the task of GV_BIND_NAME_AND_ROOT_SEARCH (in terms of setting gv_cur_region)
 * in case the global spans multiple regions.
 */
#define	GV_BIND_SUBSNAME_IF_GVSPAN(GVNH_REG, GD_HEADER, GVKEY, REG)			\
MBSTART {										\
	GBLREF gd_region	*gv_cur_region;						\
											\
	assert(NULL != GVNH_REG);							\
	if (NULL != GVNH_REG->gvspan)							\
	{										\
		GV_BIND_SUBSNAME(GVNH_REG, GD_HEADER, GVKEY, REG);			\
	} else										\
		TREF(gd_targ_gvnh_reg) = NULL;						\
} MBEND

#define	GV_BIND_SUBSNAME_FROM_GVNH_REG_IF_GVSPAN(GVNH_REG, GD_HEADER, GVKEY)						\
MBSTART {														\
	gvnh_reg_t			*gvnhReg; /* use unique name to avoid name collisions with macro caller */	\
	DEBUG_ONLY(													\
		ht_ent_mname		*tabent;									\
		GBLREF gv_namehead	*gv_target;									\
		gvnh_reg_t		*tmp_gvnhReg;									\
	)														\
															\
	gvnhReg = GVNH_REG;	/* set by op_gvname in previous call */							\
	DEBUG_ONLY(													\
		tabent = lookup_hashtab_mname((hash_table_mname *)((GD_HEADER)->tab_ptr), &gv_target->gvname);		\
		assert(NULL != tabent);											\
		tmp_gvnhReg = (gvnh_reg_t *)tabent->value;								\
		assert(NULL != tmp_gvnhReg);										\
		if (NULL != tmp_gvnhReg->gvspan)									\
			assert(tmp_gvnhReg == gvnhReg);									\
		else													\
			assert(NULL == gvnhReg);									\
	)														\
	/* A non-NULL value of gvnh_reg indicates a spanning global as confirmed by the assert below */			\
	assert((NULL == gvnhReg) || (TREF(spangbl_seen) && (NULL != gvnhReg->gvspan)));					\
	if (NULL != gvnhReg)												\
		gv_bind_subsname(GD_HEADER, GVKEY, GVNH_REG);								\
} MBEND

/* This macro is similar to GV_BIND_SUBSNAME_IF_GVSPAN except we know for sure this is a spanning global */
#define	GV_BIND_SUBSNAME(GVNH_REG, GD_HEADER, GVKEY, REG)					\
MBSTART {	/* This is a global that spans multiple regions. Re-bind the subscripted reference		\
	 * just in case this maps to a different region than the unsubscripted global name.			\
	 */													\
	TREF(gd_targ_gvnh_reg) = GVNH_REG;									\
	gv_bind_subsname(GD_HEADER, GVKEY, GVNH_REG);						\
	/* gv_target/gv_cur_region/cs_addrs/cs_data are now initialized even for non-NULL gvnh_reg->gvspan */	\
	/* In addition TREF(gd_targ_map) is set as well */							\
	REG = gv_cur_region;	/* adjust "REG" in case gv_cur_region was modified in the above call */		\
} MBEND

/* sets gv_target to correspond to REG region of spanning global. also sets gv_target->root if not already non-zero.
 * also sets global variables gv_cur_region/cs_addrs/cs_data to correspond to input region.
 */
#define	GV_BIND_SUBSREG(ADDR, REG, GVNH_REG)						\
MBSTART {										\
	gv_namehead			*gvt;						\
	gvnh_spanreg_t			*gvspan;					\
	int				min_reg_index, reg_index;			\
	DEBUG_ONLY(enum db_acc_method	acc_meth;)					\
											\
	GBLREF gv_namehead	*gv_target;						\
											\
	if (!REG->open)									\
		gv_init_reg(REG);							\
	gvspan = GVNH_REG->gvspan;							\
	assert(NULL != gvspan);								\
	min_reg_index = gvspan->min_reg_index;						\
	GET_REG_INDEX(ADDR, &ADDR->regions[0], REG, reg_index);	/* sets "reg_index" */	\
	assert(reg_index >= min_reg_index);						\
	assert(reg_index <= gvspan->max_reg_index);					\
	gvt = gvspan->gvt_array[reg_index - min_reg_index];				\
	/* Assert that this region is indeed mapped to by the spanning global */	\
	assert(INVALID_GV_TARGET != gvt);						\
	if (NULL == gvt)								\
	{										\
		gvt = targ_alloc(REG->max_key_size, &GVNH_REG->gvt->gvname, REG);	\
		COPY_ACT_FROM_GVNH_REG_TO_GVT(GVNH_REG, gvt, REG);			\
		/* See comment in GVNH_REG_INIT macro for why the below assignment is	\
		 * placed AFTER all error conditions (in above macro) have passed.	\
		 */									\
		gvspan->gvt_array[reg_index - min_reg_index] = gvt;			\
	}										\
	gv_target = gvt;								\
	/* Even though gv_cur_region might already be equal to "REG", need to invoke	\
	 * "change_reg" in order to do the "tp_set_sgm" in case of TP.			\
	 */										\
	gv_cur_region = REG;								\
	change_reg();									\
	DEBUG_ONLY(acc_meth = REG_ACC_METH(gv_cur_region);)				\
	assert(IS_ACC_METH_BG_OR_MM(acc_meth));						\
	GVCST_ROOT_SEARCH;								\
} MBEND

/* When invoking grab_lock or grab_gtmsource_srv_latch, use one of the following parameters.
 * GRAB_LOCK_ONLY			: use when code expects an online rollback, but handles it separately (like updproc.c)
 * GRAB_GTMSOURCE_SRV_LATCH_ONLY	: Same as above (just that this is used for grab_gtmsource_srv_latch instead of grab_lock)
 * ASSERT_NO_ONLINE_ROLLBACK		: use when holding some other lock (like crit) that prevents online rollback
 * HANDLE_CONCUR_ONLINE_ROLLBACK	: use when not blocking online rollback, but handling with this macro (only source server)
 */
#define	GRAB_LOCK_ONLY				0x01
#define	GRAB_GTMSOURCE_SRV_LATCH_ONLY		0x01
#define ASSERT_NO_ONLINE_ROLLBACK		0x02
#define HANDLE_CONCUR_ONLINE_ROLLBACK		0x03

/* caller should have THREADGBL_ACCESS and gbl access to t_tries and t_fail_hist */
# define LAST_RESTART_CODE			( (0 < t_tries) ? t_fail_hist[TREF(prev_t_tries)] : (enum cdb_sc)cdb_sc_normal )
# define SYNC_ONLN_RLBK_CYCLES													\
MBSTART {															\
	GBLREF	sgmnt_addrs		*cs_addrs_list;										\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool_head;										\
	GBLREF	boolean_t		mu_reorg_process;									\
																\
	sgmnt_addrs			*lcl_csa;										\
	jnlpool_addrs_ptr_t		lcl_jnlpool;										\
	DCL_THREADGBL_ACCESS;													\
																\
	SETUP_THREADGBL_ACCESS;													\
	if (!TREF(in_gvcst_bmp_mark_free) || mu_reorg_process)									\
	{															\
		for (lcl_csa = cs_addrs_list; NULL != lcl_csa; lcl_csa = lcl_csa->next_csa)					\
		{														\
			lcl_csa->onln_rlbk_cycle = lcl_csa->nl->onln_rlbk_cycle;						\
			lcl_csa->db_onln_rlbkd_cycle = lcl_csa->nl->db_onln_rlbkd_cycle;					\
			/* Online rollback increments csa->hdr->db_trigger_cycle forcing a mismatch to reload triggers */	\
			lcl_csa->db_trigger_cycle = lcl_csa->hdr->db_trigger_cycle;						\
		}														\
		for (lcl_jnlpool = jnlpool_head; NULL != lcl_jnlpool; lcl_jnlpool = lcl_jnlpool->next)				\
		{														\
			if (NULL != lcl_jnlpool->jnlpool_ctl)									\
			{													\
				lcl_csa = &FILE_INFO(lcl_jnlpool->jnlpool_dummy_reg)->s_addrs;					\
				lcl_csa->onln_rlbk_cycle = lcl_jnlpool->jnlpool_ctl->onln_rlbk_cycle;				\
			}													\
		}														\
	}															\
} MBEND
# define SYNC_ROOT_CYCLES(CSA)													\
MBSTART {	/* NULL CSA acts as a flag to do all regions */									\
	GBLREF  sgmnt_addrs             *cs_addrs_list;										\
	GBLREF	boolean_t		mu_reorg_process;									\
																\
	sgmnt_addrs                     *lcl_csa;										\
	DCL_THREADGBL_ACCESS;													\
																\
	SETUP_THREADGBL_ACCESS;													\
	for (lcl_csa = cs_addrs_list; NULL != lcl_csa; lcl_csa = lcl_csa->next_csa)						\
		if (NULL == (CSA) || (CSA) == lcl_csa)										\
			lcl_csa->root_search_cycle = lcl_csa->nl->root_search_cycle;						\
} MBEND
# define MISMATCH_ROOT_CYCLES(CSA, CNL)		((CSA)->root_search_cycle != (CNL)->root_search_cycle)
# define MISMATCH_ONLN_RLBK_CYCLES(CSA, CNL)	((CSA)->onln_rlbk_cycle != (CNL)->onln_rlbk_cycle)
# define ONLN_RLBK_STATUS(CSA, CNL)												\
	(((CSA)->db_onln_rlbkd_cycle != (CNL)->db_onln_rlbkd_cycle) ? cdb_sc_onln_rlbk2 : cdb_sc_onln_rlbk1)
# define ABORT_TRANS_IF_GBL_EXIST_NOMORE(LCL_T_TRIES, TN_ABORTED)								\
MBSTART {															\
	DEBUG_ONLY(GBLREF unsigned int		t_tries;)									\
	DEBUG_ONLY(GBLREF unsigned char		t_fail_hist[CDB_MAX_TRIES];)							\
	GBLREF	gd_region			*gv_cur_region;									\
	GBLREF	sgmnt_addrs			*cs_addrs;									\
	GBLREF	gv_namehead			*gv_target;									\
																\
	DEBUG_ONLY(enum cdb_sc			failure;)									\
	DCL_THREADGBL_ACCESS;													\
																\
	SETUP_THREADGBL_ACCESS;													\
	assert(0 < t_tries);													\
	assert((CDB_STAGNATE == t_tries) || (LCL_T_TRIES == t_tries - 1));							\
	DEBUG_ONLY(failure = LAST_RESTART_CODE);										\
	assert(NULL != gv_target);												\
	TN_ABORTED = FALSE;													\
	if (!gv_target->root)													\
	{	/* online rollback took us back to a prior logical state where the global that existed when we came into	\
		 * mu_reorg or mu_extr_getblk, no longer exists. Consider this as the end of the tree and return to the		\
		 * caller with the appropriate code. The caller knows to continue with the next global				\
		 */														\
		assert(cdb_sc_onln_rlbk2 == failure || TREF(rlbk_during_redo_root));						\
		/* abort the current transaction */										\
		t_abort(gv_cur_region, cs_addrs);										\
		TN_ABORTED = TRUE;												\
	}															\
} MBEND

#define	SET_GV_ALTKEY_TO_GBLNAME_FROM_GV_CURRKEY				\
MBSTART {									\
	uchar_ptr_t		dst_ptr, src_ptr;				\
										\
	GBLREF	gv_key		*gv_currkey, *gv_altkey;			\
	GBLREF	int4		gv_keysize;					\
										\
	assert(gv_altkey->top == gv_currkey->top);				\
	assert(gv_altkey->top == gv_keysize);					\
	assert(gv_currkey->end < gv_currkey->top);				\
	dst_ptr = gv_altkey->base;						\
	src_ptr = gv_currkey->base;						\
	for ( ; *src_ptr; )							\
		*dst_ptr++ = *src_ptr++;					\
	*dst_ptr++ = 0;								\
	*dst_ptr = 0;								\
	gv_altkey->end = dst_ptr - gv_altkey->base;				\
	assert(gv_altkey->end < gv_altkey->top);				\
} MBEND

typedef struct span_subs_struct {
	unsigned char	b_ctrl;
	unsigned char	b_first;
	unsigned char	b_sec;
} span_subs;

#define ASCII_0			'0'
#define SPAN_BLKID_BASE		255
#define DECIMAL_BASE		10

/* SPAN_SUBS_LEN macro define the length of the special subscript of spanning node in terms of number of characters.
 * The format of special spanning node subscript is '#-X-X', where X ranges from 1 to 255. Note that X never takes the
 * value of '0' because '0' is used as subscript deliminiter.
 */
#define SPAN_SUBS_LEN		3

#define SPAN_PREFIX		"#SPAN"
#define SPAN_PREFIX_LEN		(SIZEOF(SPAN_PREFIX) - 1)
#define ASGN_SPAN_PREFIX(B)	{*(B) = '#'; *(B + 1) = 'S'; *(B + 2) = 'P'; *(B + 3) = 'A'; *(B + 4) = 'N';}
#define SPAN_GVSUBS2INT(A)      (((A)->b_first - 1) * SPAN_BLKID_BASE + (A)->b_sec - 1)
#define SPAN_INT2GVSUBS(A, B)   {(A)->b_ctrl = 0x02; (A)->b_first = B / SPAN_BLKID_BASE + 1 ; (A)->b_sec = B % SPAN_BLKID_BASE + 1;}
#define SPAN_DECLSUBS(A)	(span_subs A)
#define SPAN_INITSUBS(A, B)	SPAN_INT2GVSUBS(A, B)
#define SPAN_INCRSUBS(A)	SPAN_INT2GVSUBS(A, SPAN_GVSUBS2INT(A) + 1)
#define SPAN_INCRBYSUBS(A, B)	SPAN_INT2GVSUBS(A, SPAN_GVSUBS2INT(A) + B)
#define SPAN_DECRSUBS(A, B)	SPAN_INT2GVSUBS(A, SPAN_GVSUBS2INT(A) - 1)
#define SPAN_DECRBYSUBS(A, B)	SPAN_INT2GVSUBS(A, SPAN_GVSUBS2INT(A) - B)

/* The following macro assumes that the length of the special subscript for spanning node is '3'. The assert in the macro verfies
 * that this assumption is true. In future, if the length of the special subscript for spanning node changes, the following macro
 * needs to be fixed accordingly.
 */
#define SPAN_SUBSCOPY_SRC2DST(DST, SRC) 						\
MBSTART {										\
	assert(SPAN_SUBS_LEN == 3);							\
	*((DST) + 0) = *((SRC) + 0); 							\
	*((DST) + 1) = *((SRC) + 1); 							\
	*((DST) + 2) = *((SRC) + 2);							\
} MBEND

/*#include "mv_stent.h"*/
typedef struct
{
	boolean_t		span_status;
	boolean_t		enable_jnl_format;
	boolean_t		enable_trigger_read_and_fire;
	boolean_t		ztval_gvcst_put_redo;
	mval			*val_forjnl;
	int4			blk_reserved_bytes;
#	ifdef GTM_TRIGGER
	unsigned char		*save_msp;
	unsigned char		*save_mv_chain; /* actually mv_stent ptr */
	mval			*ztold_mval;
	mval			*ztval_mval;
#	endif
} span_parms;

# define DEFINE_NSB_CONDITION_HANDLER(gvcst_xxx_ch)							\
	CONDITION_HANDLER(gvcst_xxx_ch)									\
	{												\
		int rc;											\
													\
		START_CH(TRUE);										\
		if ((int)ERR_TPRETRY == SIGNAL)								\
		{											\
			rc = tp_restart(1, !TP_RESTART_HANDLES_ERRORS);					\
			/*DEBUG_ONLY(printf("gvcst_xxx_ch: Unwinding due to TP Restart\n");)*/		\
			UNWIND(NULL, NULL);								\
		}											\
		NEXTCH;											\
	}
/* Check if the value of a primary node is a dummy value: $c(0). If so, it might be a spanning node */
# define IS_SN_DUMMY(len, addr) ((1 == (len)) && ('\0' == *(unsigned char *)(addr)))
# define RTS_ERROR_IF_SN_DISALLOWED											\
MBSTART {														\
	GBLREF	boolean_t 		span_nodes_disallowed;								\
															\
	if (span_nodes_disallowed)											\
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_UNIMPLOP, 0,	ERR_TEXT, 2,				\
					LEN_AND_LIT("GT.CM Server does not support spanning nodes"));			\
} MBEND
# define IF_SN_DISALLOWED_AND_NO_SPAN_IN_DB(STATEMENT)									\
MBSTART {	/* We've encountered a spanning node dummy value. Check if spanning nodes are disallowed (e.g., GT.CM).	\
	 * If csd->span_node_absent is TRUE we know the value really is $c(0) and we return that. Otherwise,		\
	 * this is potentially a spanning node. We cannot do an op_tstart to check so we issue an error instead.	\
	 */														\
	GBLREF	sgmnt_data_ptr_t	cs_data;									\
	GBLREF	boolean_t 		span_nodes_disallowed;								\
															\
	if (span_nodes_disallowed)											\
	{														\
		if (!cs_data->span_node_absent)										\
		{													\
			RTS_ERROR_IF_SN_DISALLOWED;									\
		} else													\
		{													\
			STATEMENT;											\
		}													\
	}														\
} MBEND

#define MAX_NSBCTRL_SZ		30	/* Upper bound on size of ctrl value. 2*10 digs + 1 comma + 1 null + overhead */

/* Control node value is 6 bytes, 2 for unsigned short numsubs, 4 for uint4 gblsize. Each is little-endian. */
#ifdef	BIGENDIAN
# define PUT_NSBCTRL(BYTES, NUMSUBS, GBLSIZE)					\
MBSTART {									\
	unsigned short	swap_numsubs;						\
	uint4		swap_gblsize;						\
										\
	swap_numsubs = GTM_BYTESWAP_16(NUMSUBS);				\
	swap_gblsize = GTM_BYTESWAP_32(GBLSIZE);				\
	PUT_USHORT((unsigned char *)BYTES, swap_numsubs);			\
	PUT_ULONG((unsigned char *)BYTES + 2, swap_gblsize);			\
} MBEND
# define GET_NSBCTRL(BYTES, NUMSUBS, GBLSIZE)					\
MBSTART {									\
	unsigned short	swap_numsubs;						\
	uint4		swap_gblsize;						\
										\
	GET_USHORT(swap_numsubs, (unsigned char *)BYTES);			\
	GET_ULONG(swap_gblsize, (unsigned char *)BYTES + 2);			\
	NUMSUBS = GTM_BYTESWAP_16(swap_numsubs);				\
	GBLSIZE = GTM_BYTESWAP_32(swap_gblsize);				\
} MBEND
#else
# define PUT_NSBCTRL(BYTES, NUMSUBS, GBLSIZE)					\
MBSTART {									\
	PUT_USHORT((unsigned char *)BYTES, NUMSUBS);				\
	PUT_ULONG((unsigned char *)BYTES + 2, GBLSIZE);				\
} MBEND
# define GET_NSBCTRL(BYTES, NUMSUBS, GBLSIZE)					\
MBSTART {									\
	GET_USHORT(NUMSUBS, (unsigned char *)BYTES);				\
	GET_ULONG(GBLSIZE, (unsigned char *)BYTES + 2);				\
} MBEND
#endif

#define	CHECK_HIDDEN_SUBSCRIPT_AND_RETURN(found, gv_altkey, is_hidden)		\
MBSTART {									\
	if (found)								\
	{									\
		CHECK_HIDDEN_SUBSCRIPT(gv_altkey, is_hidden);			\
		if (!is_hidden)							\
			return found;						\
	} else									\
		return found;							\
} MBEND
#define CHECK_HIDDEN_SUBSCRIPT(KEY, IS_HIDDEN)					\
MBSTART {									\
	sm_uc_ptr_t	keyloc;							\
										\
	keyloc = (KEY)->base + (KEY)->end - 5;					\
	if ((KEY)->end >= 5 && 0 == *keyloc && 2 == *(keyloc+1))		\
		IS_HIDDEN = TRUE;						\
	else									\
		IS_HIDDEN = FALSE;						\
} MBEND
#define SAVE_GV_CURRKEY(SAVE_KEY)						\
MBSTART {									\
	assert(NULL != gv_currkey);						\
	assert((SIZEOF(gv_key) + gv_currkey->end) <= SIZEOF(SAVE_KEY));		\
	memcpy(&SAVE_KEY[0], gv_currkey, SIZEOF(gv_key) + gv_currkey->end);	\
} MBEND
#define RESTORE_GV_CURRKEY(SAVE_KEY)						\
MBSTART {									\
	assert(gv_currkey->top == SAVE_KEY[0].top);				\
	memcpy(gv_currkey, &SAVE_KEY[0], SIZEOF(gv_key) + SAVE_KEY[0].end);	\
} MBEND
#define SAVE_GV_ALTKEY(SAVE_KEY)						\
MBSTART {									\
	assert(NULL != gv_altkey);						\
	assert((SIZEOF(gv_key) + gv_altkey->end) <= SIZEOF(SAVE_KEY));		\
	memcpy(&SAVE_KEY[0], gv_altkey, SIZEOF(gv_key) + gv_altkey->end);	\
} MBEND
#define RESTORE_GV_ALTKEY(SAVE_KEY)						\
MBSTART {									\
	assert(gv_altkey->top == SAVE_KEY[0].top);				\
	memcpy(gv_altkey, &SAVE_KEY[0], SIZEOF(gv_key) + SAVE_KEY[0].end);	\
} MBEND
#define SAVE_GV_CURRKEY_LAST_SUBSCRIPT(SAVE_KEY, PREV, OLDEND)				\
MBSTART {										\
	PREV = gv_currkey->prev;							\
	OLDEND = gv_currkey->end;							\
	assert('\0' == gv_currkey->base[oldend]);					\
	if (PREV <= OLDEND)								\
		memcpy(&SAVE_KEY[0], &gv_currkey->base[PREV], OLDEND - PREV + 1);	\
} MBEND
#define RESTORE_GV_CURRKEY_LAST_SUBSCRIPT(SAVE_KEY, PREV, OLDEND)			\
MBSTART {										\
	gv_currkey->prev = PREV;							\
	gv_currkey->end = OLDEND;							\
	if (PREV <= OLDEND)								\
		memcpy(&gv_currkey->base[PREV], &SAVE_KEY[0], OLDEND - PREV + 1);	\
	assert('\0' == gv_currkey->base[OLDEND]);					\
} MBEND

#define CAN_APPEND_HIDDEN_SUBS(KEY)	(((KEY)->end + 5 <= MAX_KEY_SZ) && ((KEY)->end + 5 <= (KEY)->top))
#define APPEND_HIDDEN_SUB(KEY)			\
MBSTART {					\
	assert(CAN_APPEND_HIDDEN_SUBS(KEY));	\
	(KEY)->end += 4;			\
	REPLACE_HIDDEN_SUB_TO_LOWEST(KEY, KEY);	\
} MBEND
#define	REPLACE_HIDDEN_SUB_TO_LOWEST(KEY1, KEY2)	\
MBSTART {						\
	int	end;					\
							\
	end = (KEY1)->end;				\
	(KEY2)->base[end - 4] = 2;			\
	(KEY2)->base[end - 3] = 1;			\
	(KEY2)->base[end - 2] = 1;			\
	(KEY2)->base[end - 1] = 0;			\
	(KEY2)->base[end - 0] = 0;			\
	assert(end < (KEY2)->top);			\
	(KEY2)->end = end;				\
} MBEND
#define	REPLACE_HIDDEN_SUB_TO_HIGHEST(KEY1, KEY2)	\
MBSTART {						\
	int	end;					\
	end = (KEY1)->end;				\
	(KEY2)->base[end - 4] = 2;			\
	(KEY2)->base[end - 3] = 0xFF;			\
	(KEY2)->base[end - 2] = 0xFF;			\
	(KEY2)->base[end - 1] = 1;			\
	(KEY2)->base[end + 0] = 0;			\
	(KEY2)->base[end + 1] = 0;			\
	assert((end + 1) < (KEY2)->top);		\
	(KEY2)->end = end + 1;				\
} MBEND

#define NEXT_HIDDEN_SUB(KEY, I)				\
MBSTART {						\
	int	end;					\
							\
	end = gv_currkey->end - 4;			\
	(KEY)->base[end++] = 2;				\
	(KEY)->base[end++] = 1 + ((I + 1) / 0xFF);	\
	(KEY)->base[end++] = 1 + ((I + 1) % 0xFF);	\
	(KEY)->base[end++] = 0;				\
	(KEY)->base[end] = 0;				\
} MBEND
#define RESTORE_CURRKEY(KEY, OLDEND)			\
MBSTART {						\
	(KEY)->end = OLDEND;				\
	(KEY)->base[OLDEND - 1] = 0; 			\
	(KEY)->base[OLDEND] = 0;			\
} MBEND
#define COMPUTE_CHUNK_SIZE(KEY, BLKSZ, RESERVED)					\
	(BLKSZ - RESERVED - ((KEY)->end + 1) - SIZEOF(blk_hdr) - SIZEOF(rec_hdr))

/* In MM mode, AIX increases native DB file size to adjust to the next nearest multiple of OS_PAGE_SIZE if the file is mapped
 * using shmat() and last portion of the file is accessed. To take into account this adjustment, following macro adjust the
 * value of DB file size determined using block count, constant DB HEADER SIZE and constant MASTER MAP size and EOF block.
 * Since we can change the DB access method using MUPIP SET and we can move the database from AIX to non-AIX platfrom,
 * following macro is not just AIX specific.
 */
#define ALIGN_DBFILE_SIZE_IF_NEEDED(SZ, NATIVE_SZ)						\
		SZ = (SZ == NATIVE_SZ) ? SZ : ROUND_UP(SZ, (OS_PAGE_SIZE / DISK_BLOCK_SIZE));

#define	SYNCIO_MORPH_SUCCESS	-1	/* a special negative value to indicate an asyncio was morphed into a syncio
					 * because the asyncio returned EAGAIN inside crit and we wanted to get the io done.
					 */

#define	ASYNCIO_ON		"ASYNCIO=ON"
#define	ASYNCIO_OFF		"ASYNCIO=OFF"
#define	TWINNING_ON(CSD)	(CSD->asyncio)	/* twinning is turned ON only when asyncio is ON */

/* Sets udi->owning_gd to the right value at db open time. */
#define SYNC_OWNING_GD(reg)									\
MBSTART {											\
	/* Either udi->owning_gd was already set to the right value, or it is NULL.		\
	 * This could be because "gvcst_init" was called previously, as in "mupip_set"		\
	 */											\
	assert((NULL != reg->owning_gd) && (NULL == FILE_INFO(reg)->owning_gd)			\
			|| (FILE_INFO(reg)->owning_gd == reg->owning_gd)			\
			|| IS_STATSDB_REG(reg));						\
	FILE_INFO(reg)->owning_gd = reg->owning_gd;						\
} MBEND

/* Wait for wip queue to be cleared or a specific CR to become non-dirty. RET is non-zero if "wcs_wtfini"
 * returns a non-zero value in any iteration. We wait a max of 1 minute if we do not see any progress in
 * the WIP queue count. If there is evidence of progress, time waited till now does not count towards the 1
 * minute timeout i.e. the 1 minute wait is started afresh.
 */
#define	WAIT_FOR_WIP_QUEUE_TO_CLEAR(CNL, CRWIPQ, CR, REG, RET)						\
MBSTART {												\
	unsigned int	lcnt;										\
	int		active_cnt, wip_cnt;								\
													\
	RET = 0; /* initialize RET in case we do not invoke "wcs_wtfini" below */			\
	DEBUG_ONLY(active_cnt = CNL->wcs_active_lvl;)	/* for debugging purposes */			\
	wip_cnt = CNL->wcs_wip_lvl;									\
	for (lcnt = 1; lcnt < (MAX_WIP_QWAIT AIX_ONLY(* 4)); lcnt++)					\
	{												\
		if (!CRWIPQ->fl)									\
			break;										\
		DEBUG_ONLY(dbg_wtfini_lcnt = lcnt);     /* used by "wcs_wtfini" */			\
		RET = wcs_wtfini(REG, CHECK_IS_PROC_ALIVE_TRUE_OR_FALSE(lcnt, BUF_OWNER_STUCK), CR);	\
		if (RET || (CR && (0 == CR->dirty)))							\
			break;										\
		wcs_sleep(lcnt);									\
		if ((wip_cnt != CNL->wcs_wip_lvl) && (NULL == CR)) /* only for non-specific crs */	\
		{	/* Change in WIP queue size. Restart wait. Note that "CNL->wcs_wip_lvl" could	\
			 * have even increased since we last noted it in "wip_cnt". This is because	\
			 * a concurrent process in "wcs_wtstart" could have moved some more records	\
			 * from the active queue to the wip queue. Restart wait even in that case.	\
			 */										\
			lcnt = 1;									\
			wip_cnt = CNL->wcs_wip_lvl;							\
		}											\
	}												\
} MBEND

#define	IS_AIO_ON(X)			(X->asyncio)
#define	IS_AIO_ON_SEG(SEG)		IS_AIO_ON(SEG)
#define	IS_AIO_ON_CSD(CSD)		IS_AIO_ON(CSD)
#define	IS_AIO_DBGLDMISMATCH(SEG, TSD)	(SEG->asyncio != TSD->asyncio)
/* This macro is invoked to avoid a DBGLDMISMATCH error. Main purpose is to copy the asyncio setting */
#define	COPY_AIO_SETTINGS(DST, SRC)	DST->asyncio = SRC->asyncio

/* Wait a max of 1 minute for pending async ios on a FD (e.g. database file) to be cleared.
 * Set TIMEDOUT variable to TRUE if we timed out. If not (i.e. async io cancellation is success) set it to FALSE.
 */
#ifdef USE_NOAIO
#define       WAIT_FOR_AIO_TO_BE_DONE(FD, TIMEDOUT)   /* NO op, N/A */
#else
#define	WAIT_FOR_AIO_TO_BE_DONE(FD, TIMEDOUT)								\
MBSTART {												\
	unsigned int	lcnt, ret, save_errno;								\
													\
	TIMEDOUT = TRUE;										\
	for (lcnt = 1; lcnt < SLEEP_ONE_MIN; lcnt++)							\
	{												\
		ret = aio_cancel(FD, NULL);								\
		save_errno = errno;									\
		assertpro(-1 != ret);									\
		assert((AIO_NOTCANCELED == ret) || (AIO_CANCELED == ret) || (AIO_ALLDONE == ret));	\
		if (AIO_NOTCANCELED != ret)								\
		{											\
			TIMEDOUT = FALSE;								\
			break;										\
		}											\
		wcs_sleep(lcnt);									\
	}												\
} MBEND
#endif

#define	OK_FOR_WCS_RECOVER_FALSE	FALSE
#define	OK_FOR_WCS_RECOVER_TRUE		TRUE

#define	LOG_ERROR_FALSE			FALSE
#define	LOG_ERROR_TRUE			TRUE

#define	SKIP_BASEDB_OPEN_FALSE		FALSE
#define	SKIP_BASEDB_OPEN_TRUE		TRUE

void		assert_jrec_member_offsets(void);
bt_rec_ptr_t	bt_put(gd_region *r, int4 block);
void		bt_que_refresh(gd_region *greg);
void		bt_init(sgmnt_addrs *cs);
void		bt_malloc(sgmnt_addrs *csa);
void		bt_refresh(sgmnt_addrs *csa, boolean_t init);
void		db_common_init(gd_region *reg, sgmnt_addrs *csa, sgmnt_data_ptr_t csd);
void		grab_crit(gd_region *reg);
boolean_t	grab_crit_encr_cycle_sync(gd_region *reg);
boolean_t	grab_crit_immediate(gd_region *reg, boolean_t ok_for_wcs_recover);
boolean_t	grab_lock(gd_region *reg, boolean_t is_blocking_wait, uint4 onln_rlbk_action);
void		gv_init_reg(gd_region *reg);
enum cdb_sc	gvincr_compute_post_incr(srch_blk_status *bh);
enum cdb_sc	gvincr_recompute_upd_array(srch_blk_status *bh, struct cw_set_element_struct *cse, cache_rec_ptr_t cr);
boolean_t	mupfndfil(gd_region *reg, mstr *mstr_addr, boolean_t log_error);
boolean_t	region_init(bool cm_regions);
freeze_status	region_freeze(gd_region *region, boolean_t freeze, boolean_t override, boolean_t wait_for_kip,
				uint4 online, boolean_t flush_sync);
freeze_status	region_freeze_main(gd_region *region, boolean_t freeze, boolean_t override, boolean_t wait_for_kip,
					uint4 online, boolean_t flush_sync);
freeze_status	region_freeze_post(gd_region *region);
void		rel_crit(gd_region *reg);
void		rel_lock(gd_region *reg);
boolean_t	wcs_verify(gd_region *reg, boolean_t expect_damage, boolean_t caller_is_wcs_recover);
void		wcs_stale(TID tid, int4 hd_len, gd_region **region);

void bmm_init(void);
int4 bmm_find_free(uint4 hint, uchar_ptr_t base_addr, uint4 total_bits);

bool reg_cmcheck(gd_region *reg);

gd_binding	*gv_srch_map(gd_addr *addr, char *key, int key_len, boolean_t skip_basedb_open);
gd_binding	*gv_srch_map_linear(gd_binding *start_map, char *key, int key_len);
gd_binding	*gv_srch_map_linear_backward(gd_binding *start_map, char *key, int key_len);
gd_gblname	*gv_srch_gblname(gd_addr *addr, char *key, int key_len);
gvnh_reg_t	*gv_bind_name(gd_addr *addr, mname_entry *targ);
void 		gv_bind_subsname(gd_addr *addr, gv_key *key, gvnh_reg_t *gvnh_reg);

void db_csh_ini(sgmnt_addrs *cs);
void db_csh_ref(sgmnt_addrs *cs_addrs, boolean_t init);
cache_rec_ptr_t db_csh_get(block_id block);
cache_rec_ptr_t db_csh_getn(block_id block);

enum cdb_sc tp_hist(srch_hist *hist1);
tp_region	*insert_region(gd_region *reg, tp_region **reg_list, tp_region **reg_free_list, int4 size);

sm_uc_ptr_t get_lmap(block_id blk, unsigned char *bits, sm_int_ptr_t cycle, cache_rec_ptr_ptr_t cr);

bool ccp_userwait(struct gd_region_struct *reg, uint4 state, int4 *timadr, unsigned short cycle);
void ccp_closejnl_ast(struct gd_region_struct *reg);
bt_rec *ccp_bt_get(sgmnt_addrs *cs_addrs, int4 block);
unsigned char *mval2subsc(mval *in_val, gv_key *out_key, boolean_t std_null_coll);

int4	dsk_read(block_id blk, sm_uc_ptr_t buff, enum db_ver *ondisk_blkver, boolean_t blk_free);

gtm_uint64_t gds_file_size(file_control *fc);

uint4	jnl_flush(gd_region *reg);
void	jnl_fsync(gd_region *reg, uint4 fsync_addr);
void	jnl_oper_user_ast(gd_region *reg);
void	jnl_wait(gd_region *reg);
void	view_jnlfile(mval *dst, gd_region *reg);
void	jnl_write_pfin(sgmnt_addrs *csa);
void	jnl_write_pini(sgmnt_addrs *csa);
void	jnl_write_epoch_rec(sgmnt_addrs *csa);
void	jnl_write_inctn_rec(sgmnt_addrs *csa);
void	fileheader_sync(gd_region *reg);

gd_addr	*create_dummy_gbldir(void);

/* These prototypes should ideally be included in gvstats_rec.h but they require "sgmnt_addrs" type
 * to be defined which is done in this header file, hence the prototyping is done here instead.
 */
void	gvstats_rec_csd2cnl(sgmnt_addrs *csa);
void	gvstats_rec_cnl2csd(sgmnt_addrs *csa);
void	gvstats_rec_upgrade(sgmnt_addrs *csa);

void act_in_gvt(gv_namehead *gvt);

#define FILE_TYPE_REPLINST	"replication instance"
#define FILE_TYPE_DB		"database"

#define NO_STATS_OPTIN		0
#define ALL_STATS_OPTIN		1
#define SOME_STATS_OPTIN	2

#include "gdsfheadsp.h"

/* End of gdsfhead.h */

#endif
