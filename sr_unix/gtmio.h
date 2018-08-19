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

/* Define macros to do our IO and restart as appropriate
 *
 * IOCTL	Loop until ioctl call succeeds or fails with other than EINTR.
 * OPENFILE	Loop until open succeeds or fails with other than EINTR.
 * OPENFILE_SYNC
 *		Loop until open succeeds or fails with other than EINTR.
 *		Opens with O_DSYNC/O_SYNC in direct mode where possible.
 *		Else opens only with O_DSYNC or O_SYNC.
 * OPEN_OBJECT_FILE	Opens the object file.
 * CLOSE_OBJECT_FILE	Close the object file.
 * CLOSEFILE	Loop until close succeeds for fails with other than EINTR.
 * CLOSEFILE_RESET
 * 		Loop until close succeeds for fails with other than EINTR.
 * 		At end reset channel to FD_INVALID unconditionally (even if close was not successful).
 * CONVERT_OBJECT_LOCK - convert type of lock held on object file
 * LSEEKREAD	Performs either pread() or an lseek()/ read() combination. In
 *		the latter case, sets global variable to warn off async IO routines.
 * LSEEKREAD_AVAILABLE	Same as LSEEKREAD except it has an extra parameter where the number of bytes
 *			ACTUALLY READ are stored irrespective of whether all REQUESTED BYTES were read or not.
 * LSEEKWRITE	Same as LSEEKREAD but for WRITE.
 * DOREADRC	Performs read, returns code 0 if okay, otherwise returns errno.
 * DOREADRL	Performs read but returns length read or -1 if errno is set.
 * DOREADRLTO2	Same as DOREADRL but has a timeout flag to poll on interrupts.
 * DOWRITE	Performs write with no error checking/return.
 * DOWRITERC	Performs write, returns code 0 if okay, otherwise returns errno.
 * DOWRITERL	Performs write but returns length written or -1 if errno is set.
 * WRITEPIPE	Performs write to FIFO/pipe but individual writes (within loop) limited to system pipe buffer size
 */

#ifndef GTMIO_Included
#define GTMIO_Included

/* Depends on aio_shim.h if using the POSIX or linux ASYNC IO #defines. This is due to aio_shim.h
 * requiring some header files with no include guards, and so we don't want to pull it in multiple
 * times by accident.
 */

#ifndef GTMIO_MINIMAL		/* Avoid pulling in includes that make gtm_icu.c uncompilable */
# include <sys/types.h>
# include "gtm_stat.h"
# include "gtm_unistd.h"
# include "gtm_fcntl.h"
# include "eintr_wrappers.h"
# include "min_max.h"
# include "wbox_test_init.h"
# include "get_fs_block_size.h"
# include "min_max.h"
#endif

#if defined(__linux__) || defined(__CYGWIN__)
#include <sys/vfs.h>
#include "is_fstype_nfs.h"
#endif

error_def(ERR_PREMATEOF);

#ifdef KEEP_zOS_EBCDIC
#define DOWRITE_A	__write_a
#define DOREAD_A	__read_a
#define	DOWRITERL_A	#error need to create as part of z/OS port and make __write_a return status and good errno
#else
#define DOWRITE_A	DOWRITE
#define DOREAD_A	read
#define	DOWRITERL_A	DOWRITERL
#endif

#define MAX_FILE_OPEN_TRIES	20  /* polling count */
#define WAIT_FOR_FILE_TIME	100 /* msec */
#define WAIT_FOR_BLOCK_TIME	100 /* msec */

#define IOCTL(FDESC, REQUEST, ARG, RC)			\
MBSTART {						\
	do						\
	{						\
		RC = ioctl(FDESC, REQUEST, ARG);	\
	} while(-1 == RC && EINTR == errno);		\
	if (-1 != RC)					\
		RC = 0;					\
	else						\
		RC = errno;				\
} MBEND

#define OPENFILE(FNAME, FFLAGS, FDESC)			\
MBSTART {						\
	do						\
	{						\
		FDESC = OPEN(FNAME, FFLAGS);		\
	} while(-1 == FDESC && EINTR == errno);		\
} MBEND

#define OPENFILE3(FNAME, FFLAGS, FMODE, FDESC)		\
MBSTART {						\
	do						\
	{						\
		FDESC = OPEN3(FNAME, FFLAGS, FMODE);	\
	} while(-1 == FDESC && EINTR == errno);		\
} MBEND

/* OPENFILE4 not needed - io_open_try handles interrupts */

/* This macro is used when we need to set FD_CLOEXEC whether or not the platform supports O_CLOEXEC.
 * This is used for system calls, e.g, pipe, that do not support O_CLOEXEC.
 */
#define SETFDCLOEXECALWAYS(FDESC) 					\
MBSTART {								\
	int flags, fcntl_res;						\
	if (-1 != FDESC) 						\
	{ 								\
	    flags = 0; 							\
	    FCNTL2(FDESC, F_GETFD, flags); 				\
	    if (-1 != flags) 						\
		FCNTL3(FDESC, F_SETFD, (flags | FD_CLOEXEC), fcntl_res);\
	} 								\
} MBEND

/* Versions of the OPEN* macros that cause the file descriptor to be closed before an EXEC. */

/* If the platform really supports O_CLOEXEC use it in the OPEN */
#if defined(O_CLOEXEC) && !defined(_AIX) && !defined(__sparc) && !defined(__APPLE__)
#define OPEN_CLOEXEC(FNAME, FFLAGS, FDESC)	\
MBSTART {					\
	FDESC = OPEN(FNAME, FFLAGS | O_CLOEXEC);\
} MBEND
#define OPEN3_CLOEXEC(FNAME, FFLAGS, FMODE, FDESC)  	\
MBSTART {						\
	FDESC = OPEN3(FNAME, FFLAGS | O_CLOEXEC, FMODE);\
} MBEND
#define OPENFILE_CLOEXEC(FNAME, FFLAGS, FDESC)  OPENFILE(FNAME, FFLAGS | O_CLOEXEC, FDESC);
#define OPENFILE_SYNC_CLOEXEC(FNAME, FFLAGS, FDESC)	OPENFILE_SYNC(FNAME, FFLAGS | O_CLOEXEC, FDESC);
#define OPENFILE3_CLOEXEC(FNAME, FFLAGS, FMODE, FDESC)	OPENFILE3(FNAME, FFLAGS | O_CLOEXEC, FMODE, FDESC);
/* The next two macros are used when the open command needs to return a value and is used as part of control statement.
 * See comment in io_open_try.c. Between the two macros (see the else case versions) either O_CLOEXEC or FD_CLOEXEC is
 * used (depending on whether the platform supports O_CLOEXEC (preferred)).
 */
#define SETOCLOEXEC(FFLAGS) (FFLAGS | O_CLOEXEC)
#define SETSOCKCLOEXEC(FFLAGS) (FFLAGS | SOCK_CLOEXEC)
/* Since the platform support O_CLOEXEC via the OPEN* no need for FD_CLOEXEC */
#define SETFDCLOEXEC(FDESC)
#else
/* If the platform does not support O_CLOEXEC, use fcntl with FD_CLOEXEC */
#define OPEN_CLOEXEC(FNAME, FFLAGS, FDESC)				\
MBSTART {								\
	int flags, fcntl_res;						\
	FDESC = OPEN(FNAME, FFLAGS);					\
	if (-1 != FDESC)						\
	{								\
	    flags = 0;							\
	    FCNTL2(FDESC, F_GETFD, flags);				\
	    if (-1 != flags)						\
		FCNTL3(FDESC, F_SETFD, (flags | FD_CLOEXEC), fcntl_res);\
	}								\
} MBEND
#define OPEN3_CLOEXEC(FNAME, FFLAGS, FMODE, FDESC)			\
MBSTART {								\
	int flags, fcntl_res;						\
	FDESC = OPEN3(FNAME, FFLAGS, FMODE);				\
	if (-1 != FDESC)						\
	{								\
	    flags = 0;							\
	    FCNTL2(FDESC, F_GETFD, flags);				\
	    if (-1 != flags)						\
		FCNTL3(FDESC, F_SETFD, (flags | FD_CLOEXEC), fcntl_res);\
	}								\
} MBEND
#define OPENFILE_CLOEXEC(FNAME, FFLAGS, FDESC)				\
MBSTART {								\
	int flags, fcntl_res;						\
	OPENFILE(FNAME, FFLAGS, FDESC);					\
	if (-1 != FDESC)						\
	{								\
	    flags = 0;							\
	    FCNTL2(FDESC, F_GETFD, flags);				\
	    if (-1 != flags)						\
		FCNTL3(FDESC, F_SETFD, (flags | FD_CLOEXEC), fcntl_res);\
	}								\
} MBEND

#define OPENFILE_SYNC_CLOEXEC(FNAME, FFLAGS, FDESC)			\
MBSTART {								\
	int flags, fcntl_res;						\
	OPENFILE_SYNC(FNAME, FFLAGS, FDESC);				\
	if (-1 != FDESC)						\
	{								\
	    flags = 0;							\
	    FCNTL2(FDESC, F_GETFD, flags);				\
	    if (-1 != flags)						\
		FCNTL3(FDESC, F_SETFD, (flags | FD_CLOEXEC), fcntl_res);\
	}								\
} MBEND

#define OPENFILE3_CLOEXEC(FNAME, FFLAGS, FMODE, FDESC)			\
MBSTART {								\
	int flags, fcntl_res;						\
	OPENFILE3(FNAME, FFLAGS, FMODE, FDESC);				\
	if (-1 != FDESC)						\
	{								\
	    flags = 0;							\
	    FCNTL2(FDESC, F_GETFD, flags);				\
	    if (-1 != flags)						\
		FCNTL3(FDESC, F_SETFD, (flags | FD_CLOEXEC), fcntl_res);\
	}								\
} MBEND
#define SETOCLOEXEC(FFLAGS) (FFLAGS)
#define SETSOCKCLOEXEC(FFLAGS) (FFLAGS)
#define SETFDCLOEXEC(FDESC)		\
MBSTART {				\
	SETFDCLOEXECALWAYS(FDESC);	\
} MBEND
#endif

#define OPENFILE_DB(FNAME, FFLAGS, UDI, SEG)									\
MBSTART {													\
	DCL_THREADGBL_ACCESS;											\
														\
	SETUP_THREADGBL_ACCESS;											\
	if (!IS_AIO_ON_SEG(SEG))										\
	{													\
		OPENFILE_CLOEXEC(FNAME, FFLAGS, UDI->fd);							\
		UDI->fd_opened_with_o_direct = FALSE;								\
	} else													\
	{													\
		OPENFILE_SYNC_CLOEXEC(FNAME, FFLAGS, UDI->fd);							\
		if (FD_INVALID != udi->fd)									\
		{												\
			UDI->fd_opened_with_o_direct = TRUE;							\
			/* Get filesystem block size and use that to align future reads/writes */		\
			UDI->db_fs_block_size = get_fs_block_size(UDI->fd);					\
			/* Until we read the db file header, we do not know the blocksize so allocate space	\
			 * initially to read the db file header. Later we can expand this to fit a GDS block	\
			 * in case that turns out to be bigger than the file header. The global variable	\
			 * "dio_buff" is filled in with the aligned/unaligned buffer details.			\
			 */											\
			DIO_BUFF_EXPAND_IF_NEEDED(UDI, SGMNT_HDR_LEN, &(TREF(dio_buff)));			\
		}												\
	}													\
} MBEND

#define FSTYPE_ADVFS	"advfs"
#define FSTYPE_UFS	"ufs"

#if defined(__MVS__)
#	define	O_DIRECT_FLAGS				(O_SYNC)
#else
#	define	O_DIRECT_FLAGS				(O_DIRECT | O_DSYNC)
#endif
#define OPENFILE_SYNC(FNAME, FFLAGS, FDESC)	OPENFILE(FNAME, FFLAGS | O_DIRECT_FLAGS, FDESC);

#if defined(__APPLE__)
#define O_DIRECT	0
#define DIRECTIO_FLAG	0
#endif

#if defined( __linux__) || defined(__CYGWIN__)
/* A special handling was needed for linux due to its inability to lock
 * over NFS.  The only difference in code is an added check for NFS file
 * thru fstatfs
 */
#define LOCK_IS_ALLOWED(FDESC, STATUS)								\
MBSTART {											\
	struct statfs buf;									\
	STATUS = ((-1 != fstatfs(FDESC, &buf)) && (NFS_SUPER_MAGIC != buf.f_type)) ? 0 : -2;	\
} MBEND
#else
#define LOCK_IS_ALLOWED(FDESC, STATUS)	STATUS = 0
#endif
#define OPEN_OBJECT_FILE(FNAME, FFLAG, FDESC)	OPENFILE(FNAME, FFLAG, FDESC)
#define CLOSE_OBJECT_FILE(FDESC, RC)		CLOSEFILE_RESET(FDESC, RC)
#define CLOSEFILE(FDESC, RC)					\
MBSTART {							\
	do							\
	{							\
		RC = close(FDESC);				\
	} while(-1 == RC && EINTR == errno);			\
	if (-1 == RC)	/* Had legitimate error - return it */	\
		RC = errno;					\
} MBEND

#define	CLOSEFILE_RESET(FDESC, RC)	\
MBSTART {				\
	CLOSEFILE(FDESC, RC);		\
	FDESC = FD_INVALID;		\
} MBEND

/* Close file only if we have it open. Use FCNTL to check if we have it open */
#define CLOSEFILE_IF_OPEN(FDESC, RC)							\
MBSTART {										\
	int	flags;									\
											\
	FCNTL2(FDESC, F_GETFL, flags);							\
	if ((-1 != flags) || (EBADF != errno))						\
		CLOSEFILE(FDESC, RC);	/* file is a valid descriptor. Close it */	\
} MBEND

#define LSEEKREAD(FDESC, FPTR, FBUFF, FBUFF_LEN, RC)						\
MBSTART {											\
	ssize_t			gtmioStatus;							\
	size_t			gtmioBuffLen;							\
	off_t			gtmioPtr;							\
	sm_uc_ptr_t		gtmioBuff;							\
	gtmioBuffLen = FBUFF_LEN;								\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);							\
	gtmioPtr = (off_t)(FPTR);								\
	for (;;)										\
	{											\
		if (-1 != (gtmioStatus = pread(FDESC, gtmioBuff, gtmioBuffLen, gtmioPtr)))	\
		{										\
			gtmioBuffLen -= gtmioStatus;						\
			if (0 == gtmioBuffLen || 0 == gtmioStatus)				\
				break;								\
			gtmioBuff += gtmioStatus;						\
			gtmioPtr += gtmioStatus;						\
			continue;								\
		}										\
		if (EINTR != errno)								\
			break;									\
	}											\
	if (0 == gtmioBuffLen)									\
		RC = 0;										\
	else if (-1 == gtmioStatus)	/* Had legitimate error - return it */			\
		RC = errno;									\
	else											\
		RC = -1;		/* Something kept us from reading what we wanted */	\
} MBEND

#define	DB_LSEEKREAD(UDI, FD, OFFSET, BUFF, SIZE, STATUS)		\
MBSTART {								\
	DBG_CHECK_DIO_ALIGNMENT(UDI, OFFSET, BUFF, SIZE);		\
	LSEEKREAD(FD, OFFSET, BUFF, SIZE, STATUS);			\
} MBEND

/* The below macro is almost the same as LSEEKREAD except it has an extra parameter where the number of
 * bytes ACTUALLY READ are stored irrespective of whether all REQUESTED BYTES were read or not.
 */
#define LSEEKREAD_AVAILABLE(FDESC, FPTR, FBUFF, FBUFF_LEN, ACTUAL_READLEN, RC)			\
MBSTART {											\
	ssize_t			gtmioStatus;							\
	size_t			gtmioBuffLen;							\
	off_t			gtmioPtr;							\
	sm_uc_ptr_t		gtmioBuff;							\
												\
	gtmioBuffLen = (FBUFF_LEN);								\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);							\
	gtmioPtr = (off_t)(FPTR);								\
	for (;;)										\
	{											\
		if (-1 != (gtmioStatus = pread(FDESC, gtmioBuff, gtmioBuffLen, gtmioPtr)))	\
		{										\
			gtmioBuffLen -= gtmioStatus;						\
			if (0 == gtmioBuffLen || 0 == gtmioStatus)				\
				break;								\
			gtmioBuff += gtmioStatus;						\
			gtmioPtr += gtmioStatus;						\
			continue;								\
		}										\
		if (EINTR != errno)								\
			break;									\
	}											\
	(ACTUAL_READLEN) = (FBUFF_LEN) - gtmioBuffLen;						\
	if (0 == gtmioBuffLen)									\
		RC = 0;										\
	else if (-1 == gtmioStatus)	/* Had legitimate error - return it */			\
		RC = errno;									\
	else											\
		RC = -1;		/* Something kept us from reading what we wanted */	\
} MBEND

#define LSEEKWRITEASYNCSTART(CSA, FDESC, FPTR, FBUFF, FBUFF_LEN, CR, RC)	\
MBSTART {									\
	memset(&CR->aiocb, 0, SIZEOF(struct aiocb));				\
	SYS_IOCB(CR).aio_nbytes = (size_t) FBUFF_LEN;				\
	SYS_IOCB(CR).aio_offset = (off_t) FPTR;					\
	LSEEKWRITEASYNCRESTART(CSA, FDESC, FBUFF, CR, RC);			\
} MBEND

#define LSEEKWRITEASYNCRESTART(CSA, FDESC, FBUFF, CR, RC)					\
MBSTART {											\
	GBLREF 	boolean_t	async_restart_got_eagain;					\
	ssize_t			gtmioStatus;							\
												\
	SYS_IOCB(CR).aio_buf = IF_LIBAIO((unsigned long)) FBUFF;				\
	SYS_IOCB(CR).aio_fildes = FDESC;							\
	assert(0 < SYS_IOCB(CR).aio_nbytes);							\
	assert(0 < SYS_IOCB(CR).aio_offset);							\
	AIO_SHIM_WRITE(CSA->region, &(CR->aiocb), gtmioStatus);					\
	if (0 == gtmioStatus)									\
		RC = 0;										\
	else if (-1 == gtmioStatus)	/* Had legitimate error - return it */			\
		RC = errno;									\
	else											\
	{											\
		assert(FALSE);									\
		RC = -1;		/* Something kept us from initiating the write */	\
	}											\
	assert((-1 < gtmioStatus) IF_LIBAIO(|| (EAGAIN == RC)));				\
	if (EAGAIN == RC)									\
		BG_TRACE_PRO_ANY(CSA, async_restart_eagain);					\
} MBEND

#define LSEEKWRITE(FDESC, FPTR, FBUFF, FBUFF_LEN, RC)						\
MBSTART {											\
	ssize_t			gtmioStatus;							\
	size_t			gtmioBuffLen;							\
	off_t			gtmioPtr;							\
	sm_uc_ptr_t		gtmioBuff;							\
												\
	gtmioBuffLen = FBUFF_LEN;								\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);							\
	gtmioPtr = (off_t)(FPTR);								\
	for (;;)										\
	{											\
		if (-1 != (gtmioStatus = pwrite(FDESC, gtmioBuff, gtmioBuffLen, gtmioPtr)))	\
		{										\
			gtmioBuffLen -= gtmioStatus;						\
			if (0 == gtmioBuffLen)							\
				break;								\
			gtmioBuff += gtmioStatus;						\
			gtmioPtr += gtmioStatus;						\
			continue;								\
		}										\
		if (EINTR != errno)								\
			break;									\
	}											\
	if (0 == gtmioBuffLen)									\
		RC = 0;										\
	else if (-1 == gtmioStatus)	/* Had legitimate error - return it */			\
		RC = errno;									\
	else											\
		RC = -1;		/* Something kept us from writing what we wanted */	\
} MBEND

#define DOREADRC(FDESC, FBUFF, FBUFF_LEN, RC)							\
MBSTART {											\
	ssize_t		gtmioStatus;								\
	size_t		gtmioBuffLen;								\
	sm_uc_ptr_t	gtmioBuff;								\
	gtmioBuffLen = FBUFF_LEN;								\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);							\
	for (;;)										\
	{											\
		if (-1 != (gtmioStatus = read(FDESC, gtmioBuff, gtmioBuffLen)))			\
		{										\
			gtmioBuffLen -= gtmioStatus;						\
			if (0 == gtmioBuffLen || 0 == gtmioStatus)				\
				break;								\
			gtmioBuff += gtmioStatus;						\
		}										\
		else if (EINTR != errno)							\
			break;									\
	}											\
	if (-1 == gtmioStatus)		/* Had legitimate error - return it */			\
		RC = errno;									\
	else if (0 == gtmioBuffLen)								\
		RC = 0;										\
	else											\
		RC = -1;		/* Something kept us from reading what we wanted */	\
} MBEND

#define DOREADRL(FDESC, FBUFF, FBUFF_LEN, RLEN)							\
MBSTART {											\
	ssize_t		gtmioStatus;								\
	size_t		gtmioBuffLen;								\
	sm_uc_ptr_t	gtmioBuff;								\
	gtmioBuffLen = FBUFF_LEN;								\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);							\
	for (;;)										\
	{											\
		if (-1 != (gtmioStatus = read(FDESC, gtmioBuff, gtmioBuffLen)))			\
		{										\
			gtmioBuffLen -= gtmioStatus;						\
			if (0 == gtmioBuffLen || 0 == gtmioStatus)				\
				break;								\
			gtmioBuff += gtmioStatus;						\
		}										\
		else if (EINTR != errno)							\
		  break;									\
	}											\
	if (-1 != gtmioStatus)									\
		RLEN = (int)(FBUFF_LEN - gtmioBuffLen); /* Return length actually read */	\
	else						/* Had legitimate error - return it */	\
		RLEN = -1;									\
} MBEND

#define DOREADRLTO2(FDESC, FBUFF, FBUFF_LEN, TOFLAG, BLOCKED_IN, ISPIPE, FLAGS, RLEN,				\
			TOT_BYTES_READ, TIMER_ID, MSEC_TIMEOUT, PIPE_ZERO_TIMEOUT, UTF_VAR_PF, PIPE_OR_FIFO)	\
MBSTART {													\
	ssize_t		gtmioStatus;										\
	int		skip_read = FALSE;									\
	int		tfcntl_res;										\
	size_t		gtmioBuffLen;										\
	sm_uc_ptr_t	gtmioBuff;										\
	gtmioBuffLen =	(size_t)FBUFF_LEN;									\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);									\
	for (;;)												\
	{													\
		/* if it is a read x:0 on a pipe and it is not blocked (always the case when starting a read)	\
		   then try and read one char.	If it succeeds then turn on blocked io and read the rest	\
		   of the line.*/										\
		if (ISPIPE && (FALSE == *BLOCKED_IN))								\
		{												\
			for (;;)										\
			{											\
				if (-1 != (gtmioStatus = read(FDESC, gtmioBuff, 1)))				\
				{										\
					if (0 == gtmioStatus) /* end of file */					\
					{									\
						skip_read = TRUE;						\
						break;								\
					}									\
					FCNTL3(FDESC, F_SETFL, FLAGS, tfcntl_res);				\
					if (0 > tfcntl_res)							\
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 		\
							5, LEN_AND_LIT("fcntl"), CALLFROM, errno);		\
					*BLOCKED_IN = TRUE;							\
					if (PIPE_ZERO_TIMEOUT)							\
					{									\
						TOFLAG = FALSE;							\
						/* Set a timer for 1 sec so atomic read x:0 will still work on	\
						   loaded systems but timeout on incomplete reads.  		\
						   Any characters						\
						   read to this point will be returned. */ 			\
						*MSEC_TIMEOUT = 1 * MILLISECS_IN_SEC;				\
						start_timer(TIMER_ID, *MSEC_TIMEOUT, wake_alarm, 0, NULL);	\
					}									\
					gtmioBuffLen -= gtmioStatus;						\
					gtmioBuff += gtmioStatus;						\
					/* if only asked to read 1 character then skip additional read */	\
					if (0 == gtmioBuffLen)							\
						skip_read = TRUE;						\
					break;									\
				} else if (EINTR != errno || TOFLAG)						\
				{										\
					skip_read = TRUE;							\
					break;									\
				}										\
			}											\
		}												\
		/* if we didn't read 1 character or it's an error don't read anymore now */			\
		if (skip_read)											\
			break;											\
		if (-1 != (gtmioStatus = read(FDESC, gtmioBuff, gtmioBuffLen)))					\
		{												\
			gtmioBuffLen -= gtmioStatus;								\
			if (0 == gtmioBuffLen || 0 == gtmioStatus)						\
				break;										\
			gtmioBuff += gtmioStatus;								\
		/* If it is pipe or fifo, read data that is currently available. If pipe contains data less than 		\
		   the CHUNK_SIZE, no need to read once again since it is in BLOCKING mode, in which case it will return -1.	\
		   So in the first read itself (after a successful read) break from the infinite loop. This variable is TRUE    \
		   if DOREADRLTO2 macro is called for a CHUNK_SIZE read. In other places (eg: iorm_get) it will be FALSE.	\
		*/												\
			if (UTF_VAR_PF)										\
				break;										\
		} else if (EINTR != errno || TOFLAG)								\
			break;											\
		if (PIPE_OR_FIFO && outofband)									\
			break;											\
	}													\
	if (-1 != gtmioStatus)											\
		RLEN = (int)(FBUFF_LEN - gtmioBuffLen);		/* Return length actually read */		\
	else						/* Had legitimate error - return it */			\
	{													\
		/* Store the number of bytes read in this invocation before we error out */			\
		*TOT_BYTES_READ = (int)(FBUFF_LEN - gtmioBuffLen);						\
		RLEN = -1;											\
	}													\
} MBEND

#define DOWRITE(FDESC, FBUFF, FBUFF_LEN)						\
MBSTART {										\
	ssize_t		gtmioStatus;							\
	size_t		gtmioBuffLen;							\
	sm_uc_ptr_t	gtmioBuff;							\
	gtmioBuffLen = FBUFF_LEN;							\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);						\
	assert(0 != gtmioBuffLen);							\
	for (;;)									\
	{										\
		if (-1 != (gtmioStatus = write(FDESC, gtmioBuff, gtmioBuffLen)))	\
		{									\
			gtmioBuffLen -= gtmioStatus;					\
			if (0 == gtmioBuffLen)						\
				break;							\
			gtmioBuff += gtmioStatus;					\
		} else if (EINTR != errno)						\
			break;								\
	}										\
	/* assertpro(FALSE)? */								\
} MBEND

#define DOWRITERC_RM(RM, FBUFF, FBUFF_LEN, RC)									\
MBSTART {													\
	if (0 == RM->fsblock_buffer_size)									\
		DOWRITERC(RM->fildes, FBUFF, FBUFF_LEN, RC);							\
	else													\
	{													\
		GBLREF	int	ydb_non_blocked_write_retries;							\
		ssize_t		gtmioStatus;									\
		size_t		gtmioBuffLen;									\
		sm_uc_ptr_t	gtmioBuff;									\
		int		block_cnt = 0;									\
		intrpt_state_t	prev_intrpt_state;								\
														\
		gtmioBuffLen = FBUFF_LEN;									\
		gtmioBuff = (sm_uc_ptr_t)(FBUFF);								\
		for (;;)											\
		{												\
			DEFER_INTERRUPTS(INTRPT_IN_IO_WRITE, prev_intrpt_state);				\
			gtmioStatus = fwrite(gtmioBuff, 1, gtmioBuffLen, RM->filstr); /* BYPASSOK("fwrite") */	\
			ENABLE_INTERRUPTS(INTRPT_IN_IO_WRITE, prev_intrpt_state);				\
			if (gtmioBuffLen >= gtmioStatus)							\
			{											\
				gtmioBuffLen -= gtmioStatus;							\
				if (0 == gtmioBuffLen)								\
					break;									\
				gtmioBuff += gtmioStatus;							\
			}											\
			else if (EINTR != errno && EAGAIN != errno)						\
				break;										\
			else if (EAGAIN == errno)								\
			{											\
				if (ydb_non_blocked_write_retries <= block_cnt)					\
					break;									\
				SHORT_SLEEP(WAIT_FOR_BLOCK_TIME);						\
				block_cnt++;									\
			}											\
		}												\
		if (0 == gtmioBuffLen)										\
			RC = 0;											\
		else												\
			RC = errno;		/* Something kept us from writing what we wanted */		\
	}													\
} MBEND

#define DOWRITERC(FDESC, FBUFF, FBUFF_LEN, RC)							\
MBSTART {											\
	GBLREF	int	ydb_non_blocked_write_retries;						\
	ssize_t		gtmioStatus;								\
	size_t		gtmioBuffLen;								\
	sm_uc_ptr_t	gtmioBuff;								\
	int		block_cnt = 0;								\
	gtmioBuffLen = FBUFF_LEN;								\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);							\
	for (;;)										\
	{											\
		if (-1 != (gtmioStatus = write(FDESC, gtmioBuff, gtmioBuffLen)))		\
		{										\
			gtmioBuffLen -= gtmioStatus;						\
			if (0 == gtmioBuffLen)							\
				break;								\
			gtmioBuff += gtmioStatus;						\
		}										\
		else if (EINTR != errno && EAGAIN != errno)					\
			break;									\
		else if (EAGAIN == errno)							\
		{										\
			if (ydb_non_blocked_write_retries <= block_cnt)				\
				break;								\
			SHORT_SLEEP(WAIT_FOR_BLOCK_TIME);					\
			block_cnt++;								\
		}										\
	}											\
	if (-1 == gtmioStatus)		/* Had legitimate error - return it */			\
		RC = errno;									\
	else if (0 == gtmioBuffLen)								\
		RC = 0;										\
	else											\
		RC = -1;		/* Something kept us from writing what we wanted */	\
} MBEND

#define DOWRITERL_RM(RM, FBUFF, FBUFF_LEN, RLEN)								\
MBSTART {													\
	if (0 == RM->fsblock_buffer_size)									\
		DOWRITERL(RM->fildes, FBUFF, FBUFF_LEN, RLEN);							\
	else													\
	{													\
		ssize_t		gtmioStatus;									\
		size_t		gtmioBuffLen;									\
		sm_uc_ptr_t	gtmioBuff;									\
		int		block_cnt = 0;									\
		intrpt_state_t	prev_intrpt_state;								\
														\
		GBLREF	int	ydb_non_blocked_write_retries;							\
														\
		gtmioBuffLen = FBUFF_LEN;									\
		gtmioBuff = (sm_uc_ptr_t)(FBUFF);								\
		for (;;)											\
		{												\
			DEFER_INTERRUPTS(INTRPT_IN_IO_WRITE, prev_intrpt_state);				\
			gtmioStatus = fwrite(gtmioBuff, 1, gtmioBuffLen, RM->filstr); /* BYPASSOK("fwrite") */	\
			ENABLE_INTERRUPTS(INTRPT_IN_IO_WRITE, prev_intrpt_state);				\
			if (gtmioBuffLen >= gtmioStatus)							\
			{											\
				gtmioBuffLen -= gtmioStatus;							\
				if (0 == gtmioBuffLen)								\
					break;									\
				gtmioBuff += gtmioStatus;							\
			}											\
			else if (EINTR != errno && EAGAIN != errno)						\
				break;										\
			else if (EAGAIN == errno)								\
			{											\
				if (ydb_non_blocked_write_retries <= block_cnt)					\
					break;									\
				SHORT_SLEEP(WAIT_FOR_BLOCK_TIME);						\
				block_cnt++;									\
			}											\
		}												\
		if (0 < gtmioStatus)										\
			RLEN = (int)(FBUFF_LEN - gtmioBuffLen); /* Return length actually written */		\
		else						/* Had legitimate error - return it */		\
			RLEN = -1;										\
	}													\
} MBEND

#define DOWRITERL(FDESC, FBUFF, FBUFF_LEN, RLEN)						\
MBSTART {											\
	ssize_t		gtmioStatus;								\
	size_t		gtmioBuffLen;								\
	sm_uc_ptr_t	gtmioBuff;								\
	int		block_cnt = 0;								\
												\
	GBLREF	int	ydb_non_blocked_write_retries;						\
												\
	gtmioBuffLen = FBUFF_LEN;								\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);							\
	for (;;)										\
	{											\
		if (-1 != (gtmioStatus = write(FDESC, gtmioBuff, gtmioBuffLen)))		\
		{										\
			gtmioBuffLen -= gtmioStatus;						\
			if (0 == gtmioBuffLen)							\
				break;								\
			gtmioBuff += gtmioStatus;						\
		}										\
		else if (EINTR != errno && EAGAIN != errno)					\
			break;									\
		else if (EAGAIN == errno)							\
		{										\
			if (ydb_non_blocked_write_retries <= block_cnt)				\
				break;								\
			SHORT_SLEEP(WAIT_FOR_BLOCK_TIME);					\
			block_cnt++;								\
		}										\
	}											\
	if (-1 != gtmioStatus)									\
		RLEN = (int)(FBUFF_LEN - gtmioBuffLen); /* Return length actually written */	\
	else						/* Had legitimate error - return it */	\
		RLEN = -1;									\
} MBEND

#define DO_FILE_READ(CHANNEL, OFFSET, READBUFF, LEN, STATUS1, STATUS2)		\
MBSTART {									\
	STATUS2 = SS_NORMAL;							\
	LSEEKREAD(CHANNEL, OFFSET, READBUFF, LEN, STATUS1);			\
	if (-1 == STATUS1)							\
		STATUS1 = ERR_PREMATEOF;					\
} MBEND

#define DB_DO_FILE_WRITE(CHANNEL, OFFSET, WRITEBUFF, LEN, STATUS1, STATUS2)				\
MBSTART {												\
	STATUS2 = SS_NORMAL;										\
	DB_LSEEKWRITE(NULL, ((unix_db_info *)NULL), NULL, CHANNEL, OFFSET, WRITEBUFF, LEN, STATUS1);	\
	if (-1 == STATUS1)										\
		STATUS1 = ERR_PREMATEOF;								\
} MBEND

#define JNL_DO_FILE_WRITE(CSA, JNL_FN, CHANNEL, OFFSET, WRITEBUFF, LEN, STATUS1, STATUS2)	\
MBSTART {											\
	STATUS2 = SS_NORMAL;									\
	JNL_LSEEKWRITE(CSA, JNL_FN, CHANNEL, OFFSET, WRITEBUFF, LEN, STATUS1);			\
	if (-1 == STATUS1)									\
		STATUS1 = ERR_PREMATEOF;							\
} MBEND

typedef struct
{
	int	fd;
	mstr	v;
} file_pointer;

/* WRITEPIPE is a work-around for a problem found in z/OS where the kernel doesn't seem to
 * break up writes into small enough pieces, requiring that we do it ourselves.  The fix is
 * applied to all Unix platforms, even though all except z/OS seem to work with monster writes.
 */
#define WRITEPIPE(FDESC, PIPESZ, FBUFF, FBUFF_LEN, RC)						\
MBSTART {											\
	GBLREF	int	ydb_non_blocked_write_retries;						\
	ssize_t		gtmioStatus;								\
	size_t		gtmioBuffLen;								\
	size_t		shortBuffLen;								\
	sm_uc_ptr_t	gtmioBuff;								\
	int		block_cnt = 0;								\
	gtmioBuffLen = FBUFF_LEN;								\
	gtmioBuff = (sm_uc_ptr_t)(FBUFF);							\
	for (;;)										\
	{											\
		shortBuffLen = MIN(PIPESZ, gtmioBuffLen);					\
		if (-1 != (gtmioStatus = write(FDESC, gtmioBuff, shortBuffLen)))		\
		{										\
			gtmioBuffLen -= gtmioStatus;						\
			if (0 == gtmioBuffLen)							\
				break;								\
			gtmioBuff += gtmioStatus;						\
		}										\
		else if (EINTR != errno && EAGAIN != errno)					\
			break;									\
		else if (EAGAIN == errno)							\
		{										\
			if (ydb_non_blocked_write_retries <= block_cnt)				\
				break;								\
			SHORT_SLEEP(WAIT_FOR_BLOCK_TIME);					\
			block_cnt++;								\
		}										\
	}											\
	if (-1 == gtmioStatus)		/* Had legitimate error - return it */			\
		RC = errno;									\
	else if (0 == gtmioBuffLen)								\
		RC = 0;										\
	else											\
		RC = -1;		/* Something kept us from writing what we wanted */	\
} MBEND

#define FFLUSH(STREAM)							\
MBSTART {								\
	intrpt_state_t	prev_intrpt_state;				\
									\
	DEFER_INTERRUPTS(INTRPT_IN_FFLUSH, prev_intrpt_state);		\
	fflush(STREAM);							\
	ENABLE_INTERRUPTS(INTRPT_IN_FFLUSH, prev_intrpt_state);		\
} MBEND

/* Macros to deal with calls which are not async-signal-safe */

#define GETC(STREAM, RC)							\
MBSTART {									\
	GBLREF boolean_t	multi_thread_in_use;				\
	char			*rname;						\
	intrpt_state_t		prev_intrpt_state;				\
	/* Use the right system call based on threads are in use or not */	\
	DEFER_INTERRUPTS(INTRPT_IN_GETC, prev_intrpt_state);			\
	if (!INSIDE_THREADED_CODE(rname))					\
		RC = getc_unlocked(STREAM);					\
	else									\
		RC = getc(STREAM);						\
	ENABLE_INTERRUPTS(INTRPT_IN_GETC, prev_intrpt_state);			\
} MBEND

#define CLEARERR(STREAM)						\
MBSTART {								\
	intrpt_state_t	prev_intrpt_state;				\
									\
	DEFER_INTERRUPTS(INTRPT_IN_IO_READ, prev_intrpt_state);		\
	clearerr(STREAM);						\
	ENABLE_INTERRUPTS(INTRPT_IN_IO_READ, prev_intrpt_state);	\
} MBEND

#define FEOF(STREAM, RC)						\
MBSTART {								\
	intrpt_state_t	prev_intrpt_state;				\
									\
	DEFER_INTERRUPTS(INTRPT_IN_IO_READ, prev_intrpt_state);		\
	RC = feof(STREAM);						\
	ENABLE_INTERRUPTS(INTRPT_IN_IO_READ, prev_intrpt_state);	\
} MBEND

#endif
