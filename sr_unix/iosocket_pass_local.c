/****************************************************************
 *								*
 * Copyright (c) 2014-2017 Fidelity National Information	*
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
#include <errno.h>
#ifdef __sun
#include <ucred.h>
#endif
#include "gtm_socket.h"
#include "gtm_unistd.h"
#include "io_params.h"
#include "io.h"
#include "iotimer.h"
#include "wake_alarm.h"
#include "iosocketdef.h"
#include "min_max.h"
#include "gtm_netdb.h"
#include "gtm_stdlib.h"
#include "gtm_string.h"
#include "eintr_wrappers.h"
#include "stringpool.h"
#include "outofband.h"
#include "error.h"

#define MAX_PASS_FDS			256

#if defined(__linux__) || defined(__sun) || defined(_AIX)
#define PID_CHECKING_SUPPORTED
#endif

#define RECVALL(FD, BUF, BUFLEN, RVAL)		XFERALL(recv, FD, BUF, BUFLEN, RVAL)
#define SENDALL(FD, BUF, BUFLEN, RVAL)		XFERALL(send, FD, BUF, BUFLEN, RVAL)

#define XFERALL(XFOP, FD, BUF, BUFLEN, RVAL)								\
{													\
	int		xf_fd = (FD);									\
	unsigned char 	*xf_buf = (unsigned char *)(BUF);						\
	size_t		xf_buflen = (BUFLEN), xf_xfercnt = 0;						\
	ssize_t		xf_rval = 0;									\
													\
	while (!outofband && !out_of_time && (xf_buflen > xf_xfercnt))					\
	{												\
		xf_rval = (XFOP)(xf_fd, xf_buf + xf_xfercnt, xf_buflen - xf_xfercnt, 0);		\
		if (-1 == xf_rval) 									\
		{											\
			if (EINTR == errno)								\
				continue;								\
			else										\
				break;									\
		}											\
		else if (0 == xf_rval)									\
		{											\
			xf_rval = -1;									\
			errno = ECONNRESET;								\
			break;										\
		}											\
		xf_xfercnt += xf_rval;									\
	}												\
	if (outofband || out_of_time)									\
	{												\
		xf_rval = -1;										\
		errno = EINTR;										\
	}												\
	RVAL = (-1 == xf_rval) ? (ssize_t)-1 : (ssize_t)xf_xfercnt;					\
}

#define PASS_COMPLETE		"PASS_COMPLETE"
#define ACCEPT_COMPLETE		"ACCEPT_COMPLETE"
#define PROTOCOL_ERROR		"Protocol Error"

GBLREF	d_socket_struct		*socket_pool;
GBLREF	io_pair			io_std_device;
GBLREF	int4			ydb_max_sockets;
GBLREF	spdesc			stringpool;
GBLREF	int			dollar_truth;
GBLREF	bool			out_of_time;
GBLREF	volatile int4		outofband;

error_def(ERR_CONNSOCKREQ);
error_def(ERR_CREDNOTPASSED);
error_def(ERR_CURRSOCKOFR);
error_def(ERR_EXPR);
error_def(ERR_LOCALSOCKREQ);
error_def(ERR_NOSOCKETINDEV);
error_def(ERR_NOSOCKHANDLE);
error_def(ERR_PEERPIDMISMATCH);
error_def(ERR_SOCKMAX);
error_def(ERR_SOCKACCEPT);
error_def(ERR_SOCKNOTFND);
error_def(ERR_SOCKNOTPASSED);
error_def(ERR_SOCKPASS);
error_def(ERR_SOCKPASSDATAMIX);
error_def(ERR_TEXT);
error_def(ERR_ZINTRECURSEIO);

pid_t get_peer_pid(int fd);

struct msgdata
{
	int			magic;
	unsigned int		proto_version;
	int			fdcount;
};

#define	MSG_MAGIC		1431655765
#define	MSG_PROTO_VERSION	1

void iosocket_pass_local(io_desc *iod, pid_t pid, int4 msec_timeout, int argcnt, va_list args)
{
	d_socket_struct 	*dsocketptr;
	socket_struct		*socketptr, *psocketptr;
	int			argn, index, rval, save_errno;
	mval			*handle;
	mstr			handlestr;
	mstr			handles[MAX_PASS_FDS];
	char			cmsg_buffer[SIZEOF(struct cmsghdr) * 2 + CMSG_SPACE(MAX_PASS_FDS * SIZEOF(int))];
	int4			cmsg_buflen;
	struct iovec		iov;
	struct msghdr		msg;
	struct cmsghdr		*cmsg;
	struct msgdata		mdata;
	int			*fds;
	pid_t			peerpid;
	TID			timer_id;
	char			complete_buf[STR_LIT_LEN(ACCEPT_COMPLETE)];
	char			*errptr;
	int4			errlen;
	boolean_t		ch_set;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (1 > argcnt)
	{
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_NOSOCKHANDLE, 0);
		return;
	}
	assert(gtmsocket == iod->type);
	dsocketptr = (d_socket_struct *)iod->dev_sp;
	if (0 >= dsocketptr->n_socket)
	{
		if (iod != io_std_device.out)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_NOSOCKETINDEV);
		return;
	}
	if (dsocketptr->n_socket <= dsocketptr->current_socket)
	{
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_CURRSOCKOFR, 2, dsocketptr->current_socket, dsocketptr->n_socket);
		return;
	}
	if (dsocketptr->mupintr)
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_ZINTRECURSEIO);
	socketptr = dsocketptr->socket[dsocketptr->current_socket];
	if (socket_local != socketptr->protocol)
	{
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_LOCALSOCKREQ, 0);
		return;
	}
	if (socket_connected != socketptr->state)
	{
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_CONNSOCKREQ, 0);
		return;
	}
	ESTABLISH_GTMIO_CH(&iod->pair, ch_set);
	ENSURE_PASS_SOCKET(socketptr);
	out_of_time = FALSE;
#	ifdef PID_CHECKING_SUPPORTED
	if (-1 != pid)
	{
		peerpid = get_peer_pid(socketptr->sd);
		if (-1 == peerpid)
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_CREDNOTPASSED, 0);
			return;
		}
		if (pid != peerpid)
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_PEERPIDMISMATCH, 2, peerpid, pid);
			return;
		}
	}
#	endif
	/* pass fds */
	fds = (int *)CMSG_DATA((struct cmsghdr *)cmsg_buffer);
	for (argn = 0; argn < argcnt; argn++)
	{
		handle = va_arg(args, mval *);
		if ((NULL == handle) || !MV_DEFINED(handle))
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_EXPR, 0);
			return;
		}
		MV_FORCE_STR(handle);
		if ((NULL == socket_pool)
			|| (0 > (index = iosocket_handle(handle->str.addr, &handle->str.len, FALSE, socket_pool))))
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_SOCKNOTFND, 2, handle->str.len, handle->str.addr);
			return;
		}
		handles[argn] = handle->str;
		psocketptr = socket_pool->socket[index];
		fds[argn] = psocketptr->sd;
	}
	/* send argcnt with fds */
	mdata.magic = MSG_MAGIC;
	mdata.proto_version = MSG_PROTO_VERSION;
	mdata.fdcount = argcnt;
	iov.iov_base = &mdata;
	iov.iov_len = SIZEOF(mdata);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_control = (struct cmsghdr *)cmsg_buffer;
	msg.msg_controllen = CMSG_SPACE(argcnt * SIZEOF(int));
	cmsg = CMSG_FIRSTHDR(&msg);
	assert(cmsg);
	cmsg->cmsg_len = CMSG_LEN(argcnt * SIZEOF(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	memcpy(iod->dollar.device, "0", SIZEOF("0"));
	if (NO_M_TIMEOUT != msec_timeout)
	{
		timer_id = (TID)iosocket_pass_local;
		start_timer(timer_id, msec_timeout, wake_alarm, 0, NULL);
	}
	do
	{
		rval = sendmsg(socketptr->sd, &msg, 0);
	}
	while (!outofband && !out_of_time && (-1 == rval) && (EINTR == errno));
	if (-1 == rval)
		goto ioerr;
	assert(rval == iov.iov_len);
	for (argn = 0; argn < argcnt; argn++)
	{
		if (0 > (index = iosocket_handle(handles[argn].addr, &handles[argn].len, FALSE, socket_pool)))
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_SOCKNOTFND, 2, handle->str.len, handle->str.addr);
			return;
		}
		psocketptr = socket_pool->socket[index];
		/* send handle length */
		SENDALL(socketptr->sd, &handles[argn].len, SIZEOF(handles[argn].len), rval);
		if (-1 == rval)
			goto ioerr;
		assert(rval == SIZEOF(handles[argn].len));
		/* send handle */
		SENDALL(socketptr->sd, handles[argn].addr, handles[argn].len, rval);
		if (-1 == rval)
			goto ioerr;
		assert(rval == handles[argn].len);
		/* send buffer length */
		SENDALL(socketptr->sd, &psocketptr->buffered_length, SIZEOF(psocketptr->buffered_length), rval);
		if (-1 == rval)
			goto ioerr;
		assert(rval == SIZEOF(psocketptr->buffered_length));
		/* send buffer */
		SENDALL(socketptr->sd, psocketptr->buffer + psocketptr->buffered_offset, psocketptr->buffered_length, rval);
		if (-1 == rval)
			goto ioerr;
		assert(rval == psocketptr->buffered_length);
	}
	SENDALL(socketptr->sd, PASS_COMPLETE, STR_LIT_LEN(PASS_COMPLETE), rval);
	if (-1 == rval)
		goto ioerr;
	assert(rval == STR_LIT_LEN(PASS_COMPLETE));
	RECVALL(socketptr->sd, complete_buf, STR_LIT_LEN(ACCEPT_COMPLETE), rval);
	if (-1 == rval)
		goto ioerr;
	assert(rval == STR_LIT_LEN(ACCEPT_COMPLETE));
	if (0 != STRNCMP_LIT(complete_buf, ACCEPT_COMPLETE))
	{
		if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
			cancel_timer(timer_id);
		iod->dollar.za = 9;
		errptr = PROTOCOL_ERROR;
		SET_DOLLARDEVICE_ONECOMMA_ERRSTR(iod, errptr);
		if (socketptr->ioerror)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_SOCKPASS, 0, ERR_TEXT, 2, STR_LIT_LEN(PROTOCOL_ERROR),
				errptr);
		REVERT_GTMIO_CH(&iod->pair, ch_set);
		return;
	}
	if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
		cancel_timer(timer_id);
	for (argn = 0; argn < argcnt; argn++)
	{
		handlestr = handles[argn];
		if (-1 != (index = iosocket_handle(handlestr.addr, &handlestr.len, FALSE, socket_pool)))
			iosocket_close_one(socket_pool, index);
	}
	if (NO_M_TIMEOUT != msec_timeout)
		dollar_truth = TRUE;
	REVERT_GTMIO_CH(&iod->pair, ch_set);
	return;

ioerr:
	save_errno = errno;
	if (out_of_time && (EINTR == save_errno))
	{
		dollar_truth = FALSE;
		REVERT_GTMIO_CH(&iod->pair, ch_set);
		return;
	}
	if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
		cancel_timer(timer_id);
	iod->dollar.za = 9;
	SET_DOLLARDEVICE_ONECOMMA_STRERROR(iod, save_errno);
	if (socketptr->ioerror)
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_SOCKPASS, 0, save_errno, 0);
	REVERT_GTMIO_CH(&iod->pair, ch_set);
	return;
}

void iosocket_accept_local(io_desc *iod, mval *handlesvar, pid_t pid, int4 msec_timeout, int argcnt, va_list args)
{
	d_socket_struct 	*dsocketptr;
	socket_struct		*socketptr, *psocketptr;
	int			argn, index, fdcount = 0, fdn, scnt = 0, rval, save_errno, handleslen = 0;
	mval			*handle, tmp;
	mstr			handles[MAX_PASS_FDS];
	mstr			handlestr;
	char			cmsg_buffer[SIZEOF(struct cmsghdr) * 2 + CMSG_SPACE(MAX_PASS_FDS * SIZEOF(int))];
	int4			cmsg_buflen;
	struct iovec		iov;
	struct msghdr		msg;
	struct cmsghdr		*cmsg = NULL;
	struct msgdata		mdata;
	int			*fds;
	char			*hptr;
	int			handlelen;
	size_t			tmpbuflen;
	pid_t			peerpid;
	TID			timer_id;
	char			complete_buf[STR_LIT_LEN(PASS_COMPLETE)];
	char			*errptr;
	int4			errlen;
	boolean_t		ch_set;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	assert(gtmsocket == iod->type);
	dsocketptr = (d_socket_struct *)iod->dev_sp;
	if (0 >= dsocketptr->n_socket)
	{
		if (iod != io_std_device.out)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_NOSOCKETINDEV);
		return;
	}
	if (dsocketptr->n_socket <= dsocketptr->current_socket)
	{
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_CURRSOCKOFR, 2, dsocketptr->current_socket, dsocketptr->n_socket);
		return;
	}
	if (dsocketptr->mupintr)
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_ZINTRECURSEIO);
	socketptr = dsocketptr->socket[dsocketptr->current_socket];
	if (socket_local != socketptr->protocol)
	{
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_LOCALSOCKREQ, 0);
		return;
	}
	if (socket_connected != socketptr->state)
	{
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_CONNSOCKREQ, 0);
		return;
	}
	ESTABLISH_GTMIO_CH(&iod->pair, ch_set);
	ENSURE_PASS_SOCKET(socketptr);
	out_of_time = FALSE;
#	ifdef PID_CHECKING_SUPPORTED
	if (-1 != pid)
	{
		peerpid = get_peer_pid(socketptr->sd);
		if (-1 == peerpid)
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_CREDNOTPASSED, 0);
			return;
		}
		if (pid != peerpid)
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_PEERPIDMISMATCH, 2, peerpid, pid);
			return;
		}
	}
#	endif
	/* accept fds */
	if (NULL == socket_pool)
		iosocket_poolinit();
	for (argn = 0; argn < argcnt; argn++)
	{
		handle = va_arg(args, mval *);
		if ((NULL != handle) && MV_DEFINED(handle))
			MV_FORCE_STR(handle);
		if ((NULL == handle) || !MV_DEFINED(handle)
				|| (-1 != iosocket_handle(handle->str.addr, &handle->str.len, FALSE, socket_pool)))
			handles[argn].addr = NULL;	/* use passed or generated handle */
		else
			handles[argn] = handle->str;
	}
	memcpy(iod->dollar.device, "0", SIZEOF("0"));
	if (NO_M_TIMEOUT != msec_timeout)
	{
		timer_id = (TID)iosocket_accept_local;
		start_timer(timer_id, msec_timeout, wake_alarm, 0, NULL);
	}
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	/* read fd count first */
	iov.iov_base = &mdata;
	iov.iov_len = SIZEOF(mdata);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = (struct cmsghdr *)cmsg_buffer;
	msg.msg_controllen = CMSG_SPACE(MAX_PASS_FDS * SIZEOF(int));
	do
	{
		rval = recvmsg(socketptr->sd, &msg, 0);
	}
	while (!outofband && !out_of_time && (-1 == rval) && (EINTR == errno));
	if (0 == rval)
	{
		rval = -1;
		errno = ECONNRESET;
	}
	if (-1 == rval)
		goto ioerr;
	if (SIZEOF(mdata) != rval)
	{
		if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
			cancel_timer(timer_id);
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_SOCKNOTPASSED, 0);
		return;
	}
	assert(rval == iov.iov_len);
	if ((MSG_MAGIC != mdata.magic) || (MSG_PROTO_VERSION != mdata.proto_version))
	{
		iod->dollar.za = 9;
		errptr = PROTOCOL_ERROR;
		SET_DOLLARDEVICE_ONECOMMA_ERRSTR(iod, errptr);
		if (socketptr->ioerror)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_SOCKACCEPT, 0, ERR_TEXT, 2, STR_LIT_LEN(PROTOCOL_ERROR),
				errptr);
	}
	cmsg = CMSG_FIRSTHDR(&msg);
	while((cmsg != NULL) && ((SOL_SOCKET != cmsg->cmsg_level) || (SCM_RIGHTS != cmsg->cmsg_type)))
		cmsg = CMSG_NXTHDR(&msg, cmsg);
	if (NULL == cmsg)
	{
		if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
			cancel_timer(timer_id);
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_SOCKNOTPASSED, 0);
		return;
	}
	fdcount = mdata.fdcount;
	fds = (int *)CMSG_DATA(cmsg);
	assert(((cmsg->cmsg_len - ((char *)CMSG_DATA(cmsg) - (char *)cmsg)) / SIZEOF(int)) == fdcount);
	if (0 == fdcount)
	{
		if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
			cancel_timer(timer_id);
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(2) ERR_SOCKNOTPASSED, 0);
		return;
	}
	if (ydb_max_sockets <= (socket_pool->n_socket + fdcount))
	{
		if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
			cancel_timer(timer_id);
		for (fdn = 0; fdn < fdcount; fdn++)
		{
			CLOSE(fds[fdn], rval);
		}
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_SOCKMAX, 1, ydb_max_sockets);
		return;
	}
	for (fdn=0; fdn < fdcount; fdn++)
	{
		/* read handle length */
		RECVALL(socketptr->sd, &handlestr.len, SIZEOF(handlestr.len), rval);
		if (-1 == rval)
			goto ioerr;
		assertpro(SIZEOF(handlestr.len) == rval);
		/* read handle */
		ENSURE_STP_FREE_SPACE(MAX_HANDLE_LEN);
		handlestr.addr = (char *)stringpool.free;
		RECVALL(socketptr->sd, handlestr.addr, handlestr.len, rval);
		if (-1 == rval)
			goto ioerr;
		assertpro(handlestr.len == rval);
		if ((fdn >= argcnt) || (NULL == handles[fdn].addr))
		{
			/* If the passed handle name already exists in the socket pool, create a new one */
			if (-1 != iosocket_handle(handlestr.addr, &handlestr.len, FALSE, socket_pool))
				iosocket_handle(handlestr.addr, &handlestr.len, TRUE, socket_pool);
			stringpool.free += handlestr.len;
			handles[fdn] = handlestr;
		}
		else
			handlestr = handles[fdn];	/* Use the handle from the argument list */
		/* read socket buffer length */
		RECVALL(socketptr->sd, &tmpbuflen, SIZEOF(tmpbuflen), rval);
		if (-1 == rval)
			goto ioerr;
		assertpro(SIZEOF(tmpbuflen) == rval);
		psocketptr = iosocket_create(NULL,
					((tmpbuflen > DEFAULT_SOCKET_BUFFER_SIZE) ? tmpbuflen : DEFAULT_SOCKET_BUFFER_SIZE),
					fds[fdn], FALSE);
		assertpro(NULL != psocketptr);
		psocketptr->handle_len = handlestr.len;
		memcpy(psocketptr->handle, handlestr.addr, handlestr.len);
		psocketptr->dev = socket_pool;
		socket_pool->socket[socket_pool->n_socket++] = psocketptr;
		socket_pool->current_socket = socket_pool->n_socket - 1;
		scnt++;
		if (0 < tmpbuflen)
		{
			/* read socket buffer */
			psocketptr->buffered_length = tmpbuflen;
			RECVALL(socketptr->sd, psocketptr->buffer, psocketptr->buffered_length, rval);
			if (-1 == rval)
				goto ioerr;
			assertpro(psocketptr->buffered_length == rval);
		}
		psocketptr->buffered_offset = 0;
		handleslen += handlestr.len;
	}
	RECVALL(socketptr->sd, complete_buf, STR_LIT_LEN(PASS_COMPLETE), rval);
	if (-1 == rval)
		goto ioerr;
	assert(rval == STR_LIT_LEN(PASS_COMPLETE));
	if (0 != STRNCMP_LIT(complete_buf, PASS_COMPLETE))
	{
		if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
			cancel_timer(timer_id);
		for (fdn = scnt - 1; fdn >= 0; fdn--)
		{
			if (-1 != (index = iosocket_handle(handles[fdn].addr, &handles[fdn].len, FALSE, socket_pool)))
				iosocket_close_one(socket_pool, index);
		}
		for (fdn = scnt; fdn < fdcount; fdn++)
		{
			CLOSE(fds[fdn], rval);
		}
		iod->dollar.za = 9;
		errptr = PROTOCOL_ERROR;
		SET_DOLLARDEVICE_ONECOMMA_ERRSTR(iod, errptr);
		if (socketptr->ioerror)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_SOCKACCEPT, 0, ERR_TEXT, 2, STR_LIT_LEN(PROTOCOL_ERROR),
				errptr);
		REVERT_GTMIO_CH(&iod->pair, ch_set);
		return;
	}
	SENDALL(socketptr->sd, ACCEPT_COMPLETE, STR_LIT_LEN(ACCEPT_COMPLETE), rval);
	if (-1 == rval)
		goto ioerr;
	assert(rval == STR_LIT_LEN(ACCEPT_COMPLETE));
	if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
		cancel_timer(timer_id);
	if (NULL != handlesvar)
	{
		handleslen += (fdcount > 1) ? (fdcount - 1) : 0;		/* space for delimiters */
		ENSURE_STP_FREE_SPACE(handleslen);
		hptr = (char *)stringpool.free;
		stringpool.free += handleslen;
		handlesvar->mvtype = MV_STR;
		handlesvar->str.addr = hptr;
		handlesvar->str.len = handleslen;
		memcpy(hptr, handles[0].addr, handles[0].len);
		hptr += handles[0].len;
		for (fdn=1; fdn < fdcount; fdn++)
		{
			*hptr++ = '|';
			memcpy(hptr, handles[fdn].addr, handles[fdn].len);
			hptr += handles[fdn].len;
		}
	}
	if (NO_M_TIMEOUT != msec_timeout)
		dollar_truth = TRUE;
	return;

ioerr:
	save_errno = errno;
	if (out_of_time && (EINTR == save_errno))
	{
		dollar_truth = FALSE;
	}
	if ((NO_M_TIMEOUT != msec_timeout) && !out_of_time)
		cancel_timer(timer_id);
	for (fdn = scnt - 1; fdn >= 0; fdn--)
	{
		if (-1 != (index = iosocket_handle(handles[fdn].addr, &handles[fdn].len, FALSE, socket_pool)))
			iosocket_close_one(socket_pool, index);
	}
	for (fdn = scnt; fdn < fdcount; fdn++)
	{
		CLOSE(fds[fdn], rval);
	}
	if ((EINTR != save_errno) || outofband)
	{
		iod->dollar.za = 9;
		SET_DOLLARDEVICE_ONECOMMA_STRERROR(iod, save_errno);
		if (socketptr->ioerror)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_SOCKACCEPT, 0, save_errno, 0);
	}
	REVERT_GTMIO_CH(&iod->pair, ch_set);
	return;
}

pid_t get_peer_pid(int fd)
{
#	if defined(__linux__)
	struct ucred		creds;
	GTM_SOCKLEN_TYPE	solen;

	solen = SIZEOF(struct ucred);
	if (-1 == getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &creds, &solen))
		return -1;
	else
		return creds.pid;
#	elif defined(_AIX)
	struct peercred_struct	creds;
	GTM_SOCKLEN_TYPE	solen;

	solen = SIZEOF(struct peercred_struct);
	if (-1 == getsockopt(fd, SOL_SOCKET, SO_PEERID, &creds, &solen))
		return -1;
	else
		return creds.pid;
#	elif defined(__sun)
	ucred_t			*credsptr = NULL;
	pid_t			peerpid;

	if (-1 == getpeerucred(fd, &credsptr))
		return -1;
	else
	{
		peerpid = ucred_getpid(credsptr);
		ucred_free(credsptr);
		return peerpid;
	}
#	else
	return -1;
#	endif
}
