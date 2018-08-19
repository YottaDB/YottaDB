/****************************************************************
 *								*
 * Copyright (c) 2001-2015 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2017 YottaDB LLC. and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/
#ifndef CMIDEFSP_H_INCLUDED
#define CMIDEFSP_H_INCLUDED

#define GTCM_SERVER_NAME	"gtcm_gnp_server" /* Service provider - for getservbyname */
#define GTCM_SERVER_PROTOCOL	"tcp"	/* Service provider - for getservbyname */
#define MAX_CONN_IND		5	/* Max. simultaneous conn. requests for listen */
#define CMM_MIN_PEER_LEVEL	"200"

/*
 * Reason codes -> cooresponds to MSG$ codes
 *
 * It is anticipated that a VAX implementation would
 * just defined the CMI_REASON_* to be the MSG$* codes
 *
 */
error_def(CMI_REASON_DISCON);
error_def(CMI_REASON_ABORT);
error_def(CMI_REASON_EXIT);
error_def(CMI_REASON_PATHLOST);
error_def(CMI_REASON_PROTOCOL);
error_def(CMI_REASON_THIRDPARTY);
error_def(CMI_REASON_TIMEOUT);
error_def(CMI_REASON_NETSHUT);
error_def(CMI_REASON_REJECT);
error_def(CMI_REASON_OVERRUN);
error_def(CMI_REASON_STATUS);
error_def(CMI_REASON_CONFIRM);

#define CMI_IO_WOULDBLOCK(iostatus)	(EWOULDBLOCK == (iostatus) || EAGAIN == (iostatus))

typedef que_head relque;
typedef int4 cmi_status_t;
typedef uint4 cmi_reason_t;
typedef int cmi_unit_t;

/* For UNIX just use an mstr */
typedef mstr cmi_descriptor;

#define CMI_DESC_LENGTH(DESC) ((unsigned)(DESC)->len)
#define CMI_DESC_SET_LENGTH(DESC, LEN) (DESC)->len = (LEN)
#define CMI_DESC_POINTER(DESC) ((DESC)->addr)
#define CMI_DESC_SET_POINTER(DESC,PTR) (DESC)->addr = (PTR)
#define CMI_DESC_CONST(DESC,LIT) (DESC)->len = strlen(LEN);(DESC)->addr = (LIT)
#define CMI_DESC_DEF(DESC,STR,LEN) (DESC)->len = (LEN);(DESC)->addr = (STR)

#include <signal.h>
#include "gtm_unistd.h"
#include "gtm_netdb.h"
#include "gtm_socket.h"	/* for sockaddr_storage */
#include "gtm_signal.h"

typedef struct
{
	unsigned short xfer_count;  /* length of currently processed packet
				     * only valid on read if len_len is 2 */
	int len_len;		    /* used to track length byte I/O */
	union {			    /* buffer for read/write of length */
		unsigned short len; /* Total length of the packet. IMPORTANT : length of a packet is always sent in network byte  */
		char lenbuf[2];	    /* order. We maintain len in network byte order until the length bytes are successfully sent. */
	} u;			    /* Thereafter we convert len back to host byte order to correctly track bytes yet to be sent. */
} qio_iosb;

typedef struct clb_stat_struct
{
	struct
	{
		uint4 msgs,errors,bytes,last_error;
	} read,write;
} clb_stat;


/*
 * Individual link connections doubly linked list
 */

#include "gtm_inet.h"
#if defined(__MVS__) || defined(__APPLE__)
/* need fd_set */
#include <sys/time.h>
#endif

struct CLB
{
	que_ent cqe;				/* forward/backward links */
	struct NTD *ntd;			/* back pointer to ntd */
	cmi_descriptor nod;			/* node */
	cmi_descriptor tnd;			/* taskname */
	struct sockaddr_storage peer_sas;	/* peer */
	struct addrinfo		peer_ai;	/* peer */
	int mun;     		                /* endpoint's file descriptor */
	void *usr;				/* client specific storage */
	qio_iosb ios;				/* used for tracking inprocess I/O */
	unsigned short cbl;			/* number of bytes read */
	unsigned short mbl;			/* max size buffer */
	unsigned char *mbf;			/* pointer to buffer */
	unsigned char urgdata;			/* buffer for urgent data */
	int fd_async;				/* TRUE --> fd is in async mode */
	int deferred_event;			/* TRUE --> deferred event signaled */
	cmi_reason_t deferred_reason;		/* reason for deferred event */
	cmi_status_t deferred_status;		/* status for deferred event */
	int sta;				/* CM_ state */
	int prev_sta;				/* CM_ state before URG WRITE, to be restored after URG WRITE completes */
	void (*err)(struct CLB *);		/* link status error callback - not used */
	void (*ast)(struct CLB *);		/* I/O call back for async read/write */
	struct clb_stat_struct stt;
};

typedef struct CLB clb_struct;

struct NTD
{
	que_ent		cqh;		/* queue of open CLB's */
	que_ent		cqh_free;	/* free list of clbs */
	boolean_t	freelist_dirty; /* TRUE --> a clb has been inserted */

		/* exception callback */
	void		(*err)(struct NTD*, struct CLB *, cmi_reason_t);	/* link status error callback */

	void		(*crq)(struct CLB *); /* connect request callback */
	bool		(*acc)(struct CLB *); /* accept connect callback */

		/* callback for urgent data */
	void		(*urg)(struct CLB *, unsigned char data);
	void		(*trc)(struct CLB *,int, unsigned char *, size_t);

	int		listen_fd;		/* Server's listen file descriptor */
	fd_set		rs,ws,es;		/* select I/O sets */
	int		max_fd;			/* largest fd - for select processing*/
	VSIG_ATOMIC_T	sigio_interrupt;
	VSIG_ATOMIC_T	sigurg_interrupt;
	/* for mutex processing - use mutex_set set to block.
	 * mutex_set - used in sigprocmask to block
	 * used in sigsuspend, sigprocmask to restore to prior
	 */
	sigset_t	mutex_set;		/* set of signals to block */
	size_t		pool_size;		/* cmi_init specified pool size */
						/* This is zero for clients */
	size_t		free_count;		/* # of items on free list */
	size_t		usr_size;		/* usr size from cmi_init */
	size_t		mbl;			/* initial mbf from cmi_init */
};

#include "iosp.h"

#define RELQUE2PTR(X) (void_ptr_t)(((unsigned char *) &(X)) + ((int4) (X)))

#define CMI_ERROR(s)		((s) != SS_NORMAL)
#define CMI_CLB_IOSTATUS(c)	((c)->deferred_status)
#define CMI_CLB_ERROR(c)	(CMI_ERROR(CMI_CLB_IOSTATUS(c)))
#define CMI_MAKE_STATUS(s)	(s)

#define CMI_MUTEX_DECL(RC)	sigset_t _cmi_oset_; int RC
#define CMI_MUTEX_BLOCK(RC)	SIGPROCMASK(SIG_BLOCK, &ntd_root->mutex_set, &_cmi_oset_, RC)
#define CMI_MUTEX_RESTORE(RC)	SIGPROCMASK(SIG_SETMASK, &_cmi_oset_, NULL, RC)

/* All TCP/IP GNP messages have a 2 byte length before the message itself */
#define CMI_TCP_PREFIX_LEN	2

#define ALIGN_QUAD	/**/

void cmi_dprint(char *cs, ...);
#ifdef DEBUG
GBLREF int cmi_debug_enabled;
#define CMI_DPRINT(x) if (cmi_debug_enabled) cmi_dprint x; else;
#else
#define CMI_DPRINT(x) /**/
#endif

#define CM_URGDATA_OFFSET        5
#define CM_URGDATA_LEN           1

#define cmi_write_int(clbp)	cmi_write_urg((clbp), *(clbp)->mbf)

#define CMI_IDLE(milliseconds)	cmi_idle((milliseconds))

/* UNIX specific cmj_ routines */
cmi_status_t cmj_set_async(int fd);
int cmj_reset_async(int fd);
cmi_status_t cmj_clb_set_async(struct CLB *lnk);
cmi_status_t cmj_clb_reset_async(struct CLB *lnk);
void cmj_err(struct CLB *lnk, cmi_reason_t reason, cmi_status_t status);
void cmj_exception_interrupt(struct CLB *lnk, int signo);
void cmj_fini(struct CLB *lnk);
int cmj_firstone(fd_set *s, int n);
struct CLB *cmj_getdeferred(struct NTD *tsk);
cmi_status_t cmj_get_port(cmi_descriptor *tnd, unsigned short *outport);
void cmj_handler(int signo, siginfo_t *info, void *context);
void cmj_housekeeping(void);
void cmj_incoming_call(struct NTD *tsk);
cmi_status_t cmj_netinit(void);
cmi_status_t cmj_postevent(struct CLB *lnk);
cmi_status_t cmj_read_start(struct CLB *lnk);
void cmj_read_interrupt(struct CLB *lnk, int signo);
void cmj_select(int signo);
cmi_status_t cmj_setupfd(int fd);
struct CLB *cmj_unit2clb(struct NTD *tsk, cmi_unit_t unit);
cmi_status_t cmj_write_start(struct CLB *lnk);
cmi_status_t cmj_write_urg_start(struct CLB *lnk);
void cmj_write_interrupt(struct CLB *lnk, int signo);
void cmj_init_clb(struct NTD *tsk, struct CLB *lnk);
cmi_status_t cmj_getsockaddr(cmi_descriptor *nod, cmi_descriptor *tnd, struct addrinfo **ai_ptr);
cmi_status_t cmi_write_urg(struct CLB *c, unsigned char data);
cmi_status_t cmi_init(
		cmi_descriptor *tnd,
		unsigned char tnr,
		void (*err)(struct NTD *ntd, struct CLB *c, cmi_reason_t reason),
		void (*crq)(struct CLB *c),
		bool (*acc)(struct CLB *c),
		void (*urg)(struct CLB *c, unsigned char data),
		size_t pool_size,
		size_t usr_size,
		size_t mbl);
void cmi_idle(uint4 hiber);
void cmi_free_clb(struct CLB *c);
struct CLB *cmi_alloc_clb(void);
unsigned char *cmi_realloc_mbf(struct CLB *lnk, size_t new_size);
void cmi_peer_info(struct CLB *lnk, char *buf, size_t sz);

#endif
