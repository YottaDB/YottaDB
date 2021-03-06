/****************************************************************
 *								*
 * Copyright (c) 2012-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef GTMSOURCE_SRV_LATCH_INCLUDED
#define GTMSOURCE_SRV_LATCH_INCLUDED

boolean_t	grab_gtmsource_srv_latch(sm_global_latch_ptr_t latch, uint4 max_timeout_in_secs, uint4 onln_rlbk_action);

boolean_t	rel_gtmsource_srv_latch(sm_global_latch_ptr_t latch);

boolean_t	gtmsource_srv_latch_held_by_us(void);

#endif
