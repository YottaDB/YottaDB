/****************************************************************
 *								*
 * Copyright (c) 2001-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef REPL_SEM_SP_H_INCLUDED
#define REPL_SEM_SP_H_INCLUDED

#define REPL_SEM_NOT_GRABBED	(EAGAIN == errno)

typedef int sem_key_t;
typedef int permissions_t;

int init_sem_set_source(sem_key_t key, int nsems, permissions_t sem_flags);
int init_sem_set_recvr(sem_key_t key, int nsems, permissions_t sem_flags);
#endif /* _REPL_SEM_SP_H */
