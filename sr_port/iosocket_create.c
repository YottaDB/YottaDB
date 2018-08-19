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

/* iosocket_create.c */
/* this module takes care of
 *	1. allocate the space
 *	2. for passive: local.sa & local.ai
 *	   for active : remote.sa & remote.ai
 *	   for $principal: via getsockname and getsockpeer
 *	3. socketptr->protocol
 *	4. socketptr->sd (initialized to -1) unless already open via inetd
 *	5. socketptr->passive
 *	6. socketptr->state (initialized to created) unless already open
 */
#include "mdef.h"

#include <errno.h>
#include "gtm_ctype.h"
#include "gtm_stdio.h"
#include "gtm_string.h"
#include "gtm_netdb.h"
#include "gtm_socket.h"
#include "gtm_un.h"
#include "gtm_inet.h"
#include "gtm_ipv6.h"
#include "gtm_stdlib.h"

#include "io.h"
#include "gt_timer.h"
#include "iosocketdef.h"
#include "min_max.h"
#include "gtm_caseconv.h"
#include "util.h"
#include "trans_log_name.h"

error_def(ERR_ADDRTOOLONG);
error_def(ERR_GETSOCKNAMERR);
error_def(ERR_GETADDRINFO);
error_def(ERR_GETNAMEINFO);
error_def(ERR_INVPORTSPEC);
error_def(ERR_INVADDRSPEC);
error_def(ERR_PROTNOTSUP);
error_def(ERR_TEXT);
error_def(ERR_SOCKINIT);

/* PORT_PROTO_FORMAT defines the format for <port>:<protocol> */
#define PORT_PROTO_FORMAT "%hu:%3[^:]"
#define	SEPARATOR ':'

socket_struct *iosocket_create(char *sockaddr, uint4 bfsize, int file_des, boolean_t listen_specified)
{
	socket_struct		*socketptr;
	socket_struct		*prev_socketptr;
	socket_struct		*socklist_head;
	boolean_t		passive = FALSE;
	unsigned short		port;
	int			ii, save_errno, tmplen, errlen, sockaddrlen;
	char 			temp_addr[SA_MAXLITLEN], protocolstr[6], *adptr;
	const char		*errptr;
	struct addrinfo		*ai_ptr;
	struct addrinfo		hints, *addr_info_ptr = NULL;
	struct sockaddr_un	*sa_un_ptr, sa_un_trans;
	mval			localpath;
	mstr			transpath;
	int			trans_status;
	enum socket_protocol	protocol;
	int			af;
	int			sd;
	int			errcode;
	char			host_buffer[NI_MAXHOST];
	char			port_buffer[NI_MAXSERV];
	int			port_buffer_len;
	int			colon_cnt, protooffset;
	char			*last_2colon = NULL;
	int			addrlen;
	GTM_SOCKLEN_TYPE	socketbuflen, tmp_addrlen;

	if (0 > file_des)
	{	/* no socket descriptor yet */
		memset(&hints, 0, SIZEOF(hints));

		protooffset = colon_cnt = 0;
		sockaddrlen = STRLEN(sockaddr);
		for (ii = sockaddrlen - 1; 0 <= ii; ii--)
		{
			if (SEPARATOR == sockaddr[ii])
			{
				colon_cnt++;
				if (1 == colon_cnt)
					protooffset = ii + 1;
				else
				{
					last_2colon = &sockaddr[ii];
					break;
				}
			}
		}
		if (0 == colon_cnt)
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_INVPORTSPEC);
			return NULL;
		}
		tmplen = sockaddrlen - protooffset;
		if (SIZEOF(protocolstr) <= tmplen)
		{	/* last piece just too big to be valid */
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_PROTNOTSUP, 2, tmplen , &sockaddr[protooffset]);
			return NULL;
		}
		lower_to_upper((uchar_ptr_t)protocolstr, (uchar_ptr_t)&sockaddr[protooffset], tmplen);
		if (((SIZEOF("TCP") - 1) == tmplen) && (0 == MEMCMP_LIT(protocolstr, "TCP")))
			protocol = socket_tcpip;
		else if (((SIZEOF("LOCAL") - 1) == tmplen) && (0 == MEMCMP_LIT(protocolstr, "LOCAL")))
			protocol = socket_local;
		else
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_PROTNOTSUP, 2, tmplen , &sockaddr[protooffset]);
			return NULL;
		}
		if (socket_tcpip == protocol)
		{
			if (1 == colon_cnt)
			{	/* for listening socket or broadcasting socket */
				if (!listen_specified || (SSCANF(sockaddr, PORT_PROTO_FORMAT, &port, protocolstr) < 2))
				{
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_INVPORTSPEC);
					return NULL;
				}
				passive = TRUE;
				/* We always first try using IPv6 address, if supported */
				af = ((GTM_IPV6_SUPPORTED && !ipv4_only) ? AF_INET6 : AF_INET);
				if (-1 == (sd = socket(af, SOCK_STREAM, IPPROTO_TCP)))
				{
					/* Try creating IPv4 socket */
					af = AF_INET;
					if (-1 == (sd = socket(af, SOCK_STREAM, IPPROTO_TCP)))
					{
						save_errno = errno;
						errptr = (char *)STRERROR(save_errno);
						errlen = STRLEN(errptr);
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_SOCKINIT, 3, save_errno,
							errlen, errptr);
						return NULL;
					}
				}
				SERVER_HINTS(hints, af);
				port_buffer_len = 0;
				I2A(port_buffer, port_buffer_len, port);
				port_buffer[port_buffer_len]='\0';
				if (0 != (errcode = getaddrinfo(NULL, port_buffer, &hints, &addr_info_ptr)))
				{
					close(sd);
					RTS_ERROR_ADDRINFO(NULL, ERR_GETADDRINFO, errcode);
					return NULL;
				}
				SOCKET_ALLOC(socketptr);
				socketptr->local.port = port;
				socketptr->temp_sd = sd;
				socketptr->sd = FD_INVALID;
				ai_ptr = &(socketptr->local.ai);
				memcpy(ai_ptr, addr_info_ptr, SIZEOF(struct addrinfo));
				SOCKET_AI_TO_LOCAL_ADDR(socketptr, addr_info_ptr);
				ai_ptr->ai_addr = SOCKET_LOCAL_ADDR(socketptr);
				ai_ptr->ai_addrlen = addr_info_ptr->ai_addrlen;
				ai_ptr->ai_next = NULL;
				freeaddrinfo(addr_info_ptr);
			} else
			{	/* connection socket */
				assert(2 == colon_cnt);
				if (listen_specified || (SSCANF(last_2colon + 1, PORT_PROTO_FORMAT, &port, protocolstr) < 2))
				{
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_INVADDRSPEC);
					return NULL;
				}
				/* for connection socket */
				SPRINTF(port_buffer, "%hu", port);
				addrlen = last_2colon - sockaddr;
				if ('[' == sockaddr[0])
				{
					if (NULL == memchr(sockaddr, ']', addrlen))
					{
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_INVADDRSPEC);
						return NULL;
					}
					addrlen -= 2;
					memcpy(temp_addr, &sockaddr[1], addrlen);
				} else
					memcpy(temp_addr, sockaddr, addrlen);
				temp_addr[addrlen] = 0;
				CLIENT_HINTS(hints);
				if (0 != (errcode = getaddrinfo(temp_addr, port_buffer, &hints, &addr_info_ptr)))
				{
					RTS_ERROR_ADDRINFO(NULL, ERR_GETADDRINFO, errcode);
					return NULL;
				}
				/*  we will test all address families in iosocket_connect() */
				SOCKET_ALLOC(socketptr);
				socketptr->remote.ai_head = addr_info_ptr;
				socketptr->remote.port = port;
				socketptr->sd = socketptr->temp_sd = FD_INVALID; /* don't mess with 0 */
			}
		} else if (socket_local == protocol)
		{	/* should we get_full_path first */
			/* check protooffset < sizeof sun_path */
			/* protooffset is after colon */
			SOCKET_ALLOC(socketptr);
			socketptr->protocol = socket_local;
			MV_INIT_STRING(&localpath, protooffset - 1, sockaddr);
			trans_status = trans_log_name(&localpath.str, &transpath, sa_un_trans.sun_path,
				(int)SIZEOF(sa_un_trans.sun_path), dont_sendmsg_on_log2long);
			if (SS_LOG2LONG == trans_status)
			{	/* if LOG2LONG, returned len not valid so report untranslated length */
				SOCKET_FREE(socketptr);
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_ADDRTOOLONG, 4, localpath.str.len, localpath.str.addr,
					localpath.str.len, SIZEOF(sa_un_trans.sun_path));
				return NULL;
			}
			sa_un_ptr = malloc(SIZEOF(struct sockaddr_un));
			sa_un_ptr->sun_family = AF_UNIX;
			memcpy(sa_un_ptr->sun_path, transpath.addr, transpath.len);
			sa_un_ptr->sun_path[transpath.len] = '\0';
			if (listen_specified)
			{
				passive = TRUE;
				socketptr->local.sa = (struct sockaddr *)sa_un_ptr;
				socketptr->local.ai.ai_family = AF_UNIX;
				socketptr->local.ai.ai_socktype = SOCK_STREAM;
				socketptr->local.ai.ai_addrlen = (size_t)((struct sockaddr_un *)0)->sun_path + protooffset;
				if (-1 == (sd = socket(AF_UNIX, SOCK_STREAM, 0)))
				{
					save_errno = errno;
					SOCKET_FREE(socketptr);
					errptr = (char *)STRERROR(save_errno);
					errlen = STRLEN(errptr);
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_SOCKINIT, 3, save_errno, errlen, errptr);
					return NULL;
				}
				socketptr->temp_sd = sd;
				socketptr->sd = FD_INVALID;
			} else
			{
				socketptr->remote.sa = (struct sockaddr *)sa_un_ptr;
				/* setup remote fields */
				socketptr->remote.ai.ai_family = AF_UNIX;
				socketptr->remote.ai.ai_socktype = SOCK_STREAM;
				socketptr->remote.ai.ai_addrlen = (size_t)((struct sockaddr_un *)0)->sun_path + protooffset;
				socketptr->sd = socketptr->temp_sd = FD_INVALID; /* don't mess with 0 */
			}
		} else
			assertpro(socket_tcpip == protocol || socket_local == protocol);	/* protocol already checked */
		socketptr->state = socket_created;
		socketptr->howcreated = passive ? creator_listen : creator_connect;
		SOCKET_BUFFER_INIT(socketptr, bfsize);
		socketptr->passive = passive;
		socketptr->moreread_timeout = DEFAULT_MOREREAD_TIMEOUT;

		return socketptr;
	} else
	{	/* socket already setup by inetd or passed via JOB or LOCAL socket */
		SOCKET_ALLOC(socketptr);
		socketptr->sd = file_des;
		socketptr->temp_sd = FD_INVALID;
		ai_ptr = &(socketptr->local.ai);
		tmp_addrlen = SIZEOF(struct sockaddr_storage);
		if (-1 == getsockname(socketptr->sd, SOCKET_LOCAL_ADDR(socketptr), &tmp_addrlen))
		{
			save_errno = errno;
			if (IS_SOCKNAME_UNIXERROR(save_errno))
			{
				SOCKET_LOCAL_ADDR(socketptr)->sa_family = AF_UNIX;
				((struct sockaddr_un *)SOCKET_LOCAL_ADDR(socketptr))->sun_path[0] = '\0';
				tmp_addrlen = SIZEOF(struct sockaddr_un);
			} else
			{
				errptr = (char *)STRERROR(save_errno);
				tmplen = STRLEN(errptr);
				SOCKET_FREE(socketptr);
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_GETSOCKNAMERR, 3, save_errno, tmplen, errptr);
				return NULL;
			}
		} else if (((size_t) (((struct sockaddr *) 0)->sa_data) >= tmp_addrlen)
						|| (0 == SOCKET_LOCAL_ADDR(socketptr)->sa_family))
		{
			SOCKET_LOCAL_ADDR(socketptr)->sa_family = AF_UNIX;
			((struct sockaddr_un *)SOCKET_LOCAL_ADDR(socketptr))->sun_path[0] = '\0';
			tmp_addrlen = SIZEOF(struct sockaddr_un);
		}
		protocol = (AF_UNIX == SOCKET_LOCAL_ADDR(socketptr)->sa_family) ? socket_local : socket_tcpip;
		ai_ptr->ai_addrlen = tmp_addrlen;
		ai_ptr->ai_family = SOCKET_LOCAL_ADDR(socketptr)->sa_family;
		ai_ptr->ai_socktype = SOCK_STREAM;
		if (socket_tcpip == protocol)
		{	/* extract port information */
			GETNAMEINFO(SOCKET_LOCAL_ADDR(socketptr), tmp_addrlen, host_buffer, NI_MAXHOST, port_buffer,
				NI_MAXSERV, NI_NUMERICHOST|NI_NUMERICSERV, errcode);
			if (0 != errcode)
			{
				SOCKET_FREE(socketptr);
				RTS_ERROR_ADDRINFO(NULL, ERR_GETNAMEINFO, errcode);
				return NULL;
			}
			STRNDUP(host_buffer, NI_MAXHOST, socketptr->local.saddr_ip);
			socketptr->local.port = ATOI(port_buffer);
			tmp_addrlen = SIZEOF(struct sockaddr_storage);
			if (-1 == getpeername(socketptr->sd, SOCKET_REMOTE_ADDR(socketptr), &tmp_addrlen))
			{
				save_errno = errno;
				errptr = (char *)STRERROR(save_errno);
				tmplen = STRLEN(errptr);
				SOCKET_FREE(socketptr);
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_GETSOCKNAMERR, 3, save_errno, tmplen, errptr);
				return NULL;
			}
		} else if (socket_local == protocol)
		{
			SOCKET_REMOTE_ADDR(socketptr)->sa_family = AF_UNIX;
			((struct sockaddr_un *)SOCKET_REMOTE_ADDR(socketptr))->sun_path[0] = '\0';
			tmp_addrlen = SIZEOF(struct sockaddr_un);
		}
		/* Even though the socket might be inherited from a JOB command or xinetd, it is possible the socket
		 * is a LISTENING or a CONNECTED socket. Determine that before assigning the state of this socket.
		 */
		socketbuflen = SIZEOF(socketptr->bufsiz);
		/* OSE/SMH - Mac OS X High Sierra (10.13.6) does not seem to accept SO_ACCEPTCONN. */
#ifndef __APPLE__
		if (-1 == getsockopt(socketptr->sd, SOL_SOCKET, SO_ACCEPTCONN, &socketptr->bufsiz, &socketbuflen))
		{
			save_errno = errno;
			errptr = (char *)STRERROR(save_errno);
			errlen = STRLEN(errptr);
			SOCKET_FREE(socketptr);
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_GETSOCKOPTERR, 5,
				RTS_ERROR_LITERAL("SO_ACCEPTCONN"), save_errno, errlen, errptr);
			return NULL;
		} else
#endif
		if (socketptr->bufsiz)
		{
			socketptr->state = socket_listening;
			socketptr->passive = TRUE;
		} else
		{
			socketptr->state = socket_connected;
			socketptr->passive = FALSE;
		}
		if (socket_connected == socketptr->state)
		{
			socketptr->remote.ai.ai_addrlen = tmp_addrlen;
			assert(0 != SOCKET_REMOTE_ADDR(socketptr)->sa_family);
			socketptr->remote.ai.ai_family = SOCKET_REMOTE_ADDR(socketptr)->sa_family;
			socketptr->remote.ai.ai_socktype = SOCK_STREAM;
			assert((socket_tcpip != protocol) || (0 != SOCKET_REMOTE_ADDR(socketptr)->sa_family));
			if (socket_tcpip == protocol)
			{
				GETNAMEINFO(SOCKET_REMOTE_ADDR(socketptr), socketptr->remote.ai.ai_addrlen, host_buffer,
					NI_MAXHOST, port_buffer, NI_MAXSERV, NI_NUMERICHOST|NI_NUMERICSERV, errcode);
				if (0 != errcode)
				{
					SOCKET_FREE(socketptr);
					RTS_ERROR_ADDRINFO(NULL, ERR_GETNAMEINFO, errcode);
					return NULL;
				}
				STRNDUP(host_buffer, NI_MAXHOST, socketptr->remote.saddr_ip);
				socketptr->remote.port = ATOI(port_buffer);
			}
		}
		socketptr->protocol = protocol;
		SOCKET_BUFFER_INIT(socketptr, bfsize);
		socketptr->howcreated = (2 >= file_des) ? creator_principal : creator_passed;
		socketptr->moreread_timeout = DEFAULT_MOREREAD_TIMEOUT;
		return socketptr;
	}
}
