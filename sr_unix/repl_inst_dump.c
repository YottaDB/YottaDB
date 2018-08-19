/****************************************************************
 *								*
 * Copyright (c) 2006-2017 Fidelity National Information	*
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

#include "mdef.h"

#include "gtm_stdio.h"
#include "gtm_string.h"

#include <stddef.h>	/* for "offsetof" macro */

#include "cli.h"
#include "util.h"
#include "repl_instance.h"
#include "gdsroot.h"
#include "gdsbt.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsfhead.h"
#include "filestruct.h"
#include "jnl.h"		/* for JNL_WHOLE_FROM_SHORT_TIME */
#include "buddy_list.h"		/* needed for muprec.h */
#include "hashtab_int4.h"	/* needed for muprec.h */
#include "hashtab_int8.h"	/* needed for muprec.h */
#include "hashtab_mname.h"	/* needed for muprec.h */
#include "muprec.h"
#include "repl_msg.h"
#include "gtmsource.h"
#include "gtmrecv.h"
#include "repl_inst_dump.h"
#include "repl_log.h"		/* for "repl_log" prototype */

LITDEF	char	state_array[][23] = {
			"DUMMY_STATE",
			"START",
			"WAITING_FOR_CONNECTION",
			"WAITING_FOR_RESTART",
			"SEARCHING_FOR_RESTART",
			"SENDING_JNLRECS",
			"WAITING_FOR_XON",
			"CHANGING_MODE"
		};

#define	PREFIX_FILEHDR			"HDR "
#define	PREFIX_SRCLCL			"SLT #!2UL : "
#define	PREFIX_HISTINFO			"HST #!7UL : "
#define	PREFIX_JNLPOOLCTL		"CTL "
#define	PREFIX_SOURCELOCAL		"SRC #!2UL : "

#define	FILEHDR_TITLE_STRING		"File Header"
#define	SRCLCL_TITLE_STRING		"Source Server Slots"
#define	HISTINFO_TITLE_STRING		"History Records (histinfo structures)"
#define	JNLPOOLCTL_TITLE_STRING		"Journal Pool Control Structure"
#define	SOURCELOCAL_TITLE_STRING	"Source Server Structures"

#define	PRINT_BOOLEAN(printstr, value, index)								\
{													\
	char	*string;										\
													\
	string = ((TRUE == (value)) ? "TRUE" : ((FALSE == (value)) ? "FALSE" : "UNKNOWN"));		\
	if ((FALSE == value) || (TRUE == value))							\
	{												\
		if (-1 == (index))									\
			util_out_print(printstr, TRUE, string);						\
		else											\
			util_out_print(printstr, TRUE, (index), string);				\
	} else												\
	{												\
		if (-1 == (index))									\
			util_out_print(printstr " [0x!XL]", TRUE, string, (value));			\
		else											\
			util_out_print(printstr " [0x!XL]", TRUE, (index), string, (value));		\
	}												\
}

#define	PRINT_TIME(printstr, value)									\
{													\
	jnl_proc_time	whole_time;									\
	int		time_len;									\
	char		time_str[LENGTH_OF_TIME + 1];							\
													\
	if (0 != value)											\
	{												\
		JNL_WHOLE_FROM_SHORT_TIME(whole_time, value);						\
		time_len = format_time(whole_time, time_str, SIZEOF(time_str), SHORT_TIME_FORMAT);	\
	} else												\
	{												\
		time_len = 1;										\
		time_str[0] = '0';									\
	}												\
	util_out_print( printstr "!R20AD", TRUE, time_len, time_str);					\
}

#define	PRINT_SEM_SHM_ID(printstr, value)						\
{											\
	assert(INVALID_SEMID == INVALID_SHMID);						\
	if (INVALID_SEMID != value)							\
		util_out_print( printstr "!10UL [0x!XL]", TRUE, value, value);		\
	else										\
		util_out_print( printstr "   INVALID", TRUE);				\
}

#define	PRINT_OFFSET_HEADER			if (detail_specified) { util_out_print("Offset     Size", TRUE); }
#define	PRINT_OFFSET_PREFIX(offset, size)								\
	if (detail_specified) { util_out_print("0x!XL 0x!4XW ", FALSE, offset + section_offset, size); };

GBLREF	uint4		section_offset;		/* Used by PRINT_OFFSET_PREFIX macro in repl_inst_dump.c */

void	repl_inst_dump_filehdr(repl_inst_hdr_ptr_t repl_instance)
{
	char		dststr[MAX_DIGITS_IN_INT], dstlen;
	char		*string;
	int4		minorver, nodename_len, last_histinfo_num;
	int		idx, strm_idx, offset;
	repl_inst_uuid	*strm_group_info;
	seq_num		strm_seqno;
	uchar_ptr_t	nodename_ptr;

	util_out_print("", TRUE);
	PRINT_DASHES;
	util_out_print(FILEHDR_TITLE_STRING, TRUE);
	PRINT_DASHES;
	PRINT_OFFSET_HEADER;

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, label[0]), SIZEOF(repl_instance->label));
	util_out_print( PREFIX_FILEHDR "Label (contains Major Version)             !11AD", TRUE,
		GDS_REPL_INST_LABEL_SZ - 1, repl_instance->label);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, replinst_minorver), SIZEOF(repl_instance->replinst_minorver));
	dstlen = 0;
	minorver = repl_instance->replinst_minorver; /* store minorver in a > 1-byte sized variable to avoid warning in I2A macro */
	I2A(dststr, dstlen, minorver);
	assert(dstlen <= 3);
	util_out_print( PREFIX_FILEHDR "Minor Version                                      !R3AD", TRUE, dstlen, dststr);

	/* Assert that the endianness of the instance file matches the endianness of the YottaDB version
	 * as otherwise we would have errored out long before reaching here.
	 */
#	ifdef BIGENDIAN
	assert(!repl_instance->is_little_endian);
#	else
	assert(repl_instance->is_little_endian);
#	endif
	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, is_little_endian), SIZEOF(repl_instance->is_little_endian));
	util_out_print( PREFIX_FILEHDR "Endian Format                                   !6AZ", TRUE, ENDIANTHISJUSTIFY);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, is_64bit), SIZEOF(repl_instance->is_64bit));
	util_out_print( PREFIX_FILEHDR "64-bit Format                                    !5AZ", TRUE,
		repl_instance->is_64bit ? " TRUE" : "FALSE");

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, jnlpool_semid), SIZEOF(repl_instance->jnlpool_semid));
	PRINT_SEM_SHM_ID( PREFIX_FILEHDR "Journal Pool Sem Id                         ", repl_instance->jnlpool_semid);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, jnlpool_shmid), SIZEOF(repl_instance->jnlpool_shmid));
	PRINT_SEM_SHM_ID( PREFIX_FILEHDR "Journal Pool Shm Id                         ", repl_instance->jnlpool_shmid);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, recvpool_semid), SIZEOF(repl_instance->recvpool_semid));
	PRINT_SEM_SHM_ID( PREFIX_FILEHDR "Receive Pool Sem Id                         ", repl_instance->recvpool_semid);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, recvpool_shmid), SIZEOF(repl_instance->recvpool_shmid));
	PRINT_SEM_SHM_ID( PREFIX_FILEHDR "Receive Pool Shm Id                         ", repl_instance->recvpool_shmid);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, jnlpool_semid_ctime), SIZEOF(repl_instance->jnlpool_semid_ctime));
	PRINT_TIME( PREFIX_FILEHDR "Journal Pool Sem Create Time      ", repl_instance->jnlpool_semid_ctime);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, jnlpool_shmid_ctime), SIZEOF(repl_instance->jnlpool_shmid_ctime));
	PRINT_TIME( PREFIX_FILEHDR "Journal Pool Shm Create Time      ", repl_instance->jnlpool_shmid_ctime);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, recvpool_semid_ctime), SIZEOF(repl_instance->recvpool_semid_ctime));
	PRINT_TIME( PREFIX_FILEHDR "Receive Pool Sem Create Time      ", repl_instance->recvpool_semid_ctime);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, recvpool_shmid_ctime), SIZEOF(repl_instance->recvpool_shmid_ctime));
	PRINT_TIME( PREFIX_FILEHDR "Receive Pool Shm Create Time      ", repl_instance->recvpool_shmid_ctime);

	/**************** Dump the "inst_info" structure member ****************/
	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, inst_info) + offsetof(repl_inst_uuid, created_nodename[0]),
				SIZEOF(repl_instance->inst_info.created_nodename));
	/* created_nodename can contain upto MAX_NODENAME_LEN characters. If it consumes the entire array, then
	 * the last character will NOT be null terminated. Check and set the array length to be used for the call
	 * to util_out_print
	 */
	nodename_ptr = repl_instance->inst_info.created_nodename;
	DBG_CHECK_CREATED_NODENAME(nodename_ptr);
	nodename_len = ('\0' == nodename_ptr[MAX_NODENAME_LEN - 1]) ? STRLEN((char *)nodename_ptr) : MAX_NODENAME_LEN;
	util_out_print( PREFIX_FILEHDR "Instance File Created Nodename        !R16AD", TRUE,
		nodename_len, nodename_ptr);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, inst_info) + offsetof(repl_inst_uuid, this_instname[0]),
				SIZEOF(repl_instance->inst_info.this_instname));
	util_out_print( PREFIX_FILEHDR "Instance Name                          !R15AD", TRUE,
		LEN_AND_STR((char *)repl_instance->inst_info.this_instname));

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, inst_info) + offsetof(repl_inst_uuid, created_time),
				SIZEOF(repl_instance->inst_info.created_time));
	PRINT_TIME( PREFIX_FILEHDR "Instance File Create Time         ", repl_instance->inst_info.created_time);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, inst_info) + offsetof(repl_inst_uuid, creator_pid),
				SIZEOF(repl_instance->inst_info.creator_pid));
	util_out_print( PREFIX_FILEHDR "Instance File Creator Pid                   !10UL [0x!XL]", TRUE,
		repl_instance->inst_info.creator_pid, repl_instance->inst_info.creator_pid);

	/**************** Dump the "lms_group_info" structure member only if it is non-null ****************/
	if (IS_REPL_INST_UUID_NON_NULL(repl_instance->lms_group_info))
	{
		PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, lms_group_info) + offsetof(repl_inst_uuid, created_nodename[0]),
					SIZEOF(repl_instance->lms_group_info.created_nodename));
		/* created_nodename can contain upto MAX_NODENAME_LEN characters. If it consumes the entire array, then
		 * the last character will NOT be null terminated. Check and set the array length to be used for the call
		 * to util_out_print
		 */
		nodename_ptr = repl_instance->lms_group_info.created_nodename;
		DBG_CHECK_CREATED_NODENAME(nodename_ptr);
		nodename_len = ('\0' == nodename_ptr[MAX_NODENAME_LEN - 1]) ? STRLEN((char *)nodename_ptr) : MAX_NODENAME_LEN;
		util_out_print( PREFIX_FILEHDR "LMS Group Created Nodename            !R16AD", TRUE,
			nodename_len, nodename_ptr);

		PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, lms_group_info) + offsetof(repl_inst_uuid, this_instname[0]),
					SIZEOF(repl_instance->lms_group_info.this_instname));
		util_out_print( PREFIX_FILEHDR "LMS Group Instance Name                !R15AD", TRUE,
			LEN_AND_STR((char *)repl_instance->lms_group_info.this_instname));

		PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, lms_group_info) + offsetof(repl_inst_uuid, creator_pid),
					SIZEOF(repl_instance->lms_group_info.creator_pid));
		util_out_print( PREFIX_FILEHDR "LMS Group Creator Pid                       !10UL [0x!XL]", TRUE,
			repl_instance->lms_group_info.creator_pid, repl_instance->lms_group_info.creator_pid);

		PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, lms_group_info) + offsetof(repl_inst_uuid, created_time),
					SIZEOF(repl_instance->lms_group_info.created_time));
		PRINT_TIME( PREFIX_FILEHDR "LMS Group Create Time             ", repl_instance->lms_group_info.created_time);
	}
	/**************** Dump the remaining members ****************/
	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, jnl_seqno), SIZEOF(repl_instance->jnl_seqno));
	util_out_print( PREFIX_FILEHDR "Journal Sequence Number           !20@UQ [0x!16@XQ]", TRUE,
			&repl_instance->jnl_seqno, &repl_instance->jnl_seqno);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, root_primary_cycle), SIZEOF(repl_instance->root_primary_cycle));
	util_out_print( PREFIX_FILEHDR "Root Primary Cycle                          !10UL [0x!XL]", TRUE,
		repl_instance->root_primary_cycle, repl_instance->root_primary_cycle);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, num_histinfo), SIZEOF(repl_instance->num_histinfo));
	util_out_print( PREFIX_FILEHDR "Number of used history records              !10UL [0x!XL]", TRUE,
		repl_instance->num_histinfo, repl_instance->num_histinfo);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, num_alloc_histinfo), SIZEOF(repl_instance->num_alloc_histinfo));
	util_out_print( PREFIX_FILEHDR "Allocated history records                   !10UL [0x!XL]", TRUE,
		repl_instance->num_alloc_histinfo, repl_instance->num_alloc_histinfo);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, crash), SIZEOF(repl_instance->crash));
	PRINT_BOOLEAN(PREFIX_FILEHDR "Crash                                       !R10AZ", repl_instance->crash, -1);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, was_rootprimary), SIZEOF(repl_instance->was_rootprimary));
	PRINT_BOOLEAN(PREFIX_FILEHDR "Root Primary                                !R10AZ", repl_instance->was_rootprimary, -1);

	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, is_supplementary), SIZEOF(repl_instance->is_supplementary));
	PRINT_BOOLEAN(PREFIX_FILEHDR "Supplementary Instance                      !R10AZ", repl_instance->is_supplementary, -1);

	/**************** Dump the supplementary information ONLY if it is a supplementary instance ****************/
	if (repl_instance->is_supplementary)
	{
		for (idx = 0; idx < MAX_SUPPL_STRMS; idx++)
		{	/* Dump information for each stream as long as it contains valid data. */
			assert(SIZEOF(last_histinfo_num) == SIZEOF(repl_instance->last_histinfo_num[0]));
			last_histinfo_num = repl_instance->last_histinfo_num[idx];
			if (INVALID_HISTINFO_NUM != last_histinfo_num)
			{
				PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, last_histinfo_num[0])
					+ (idx * SIZEOF(last_histinfo_num)), SIZEOF(last_histinfo_num));
				util_out_print( PREFIX_FILEHDR "STRM !2UL: Last history record number         !10UL [0x!XL]", TRUE,
					idx, last_histinfo_num, last_histinfo_num);
			}
			assert(SIZEOF(strm_seqno) == SIZEOF(repl_instance->strm_seqno[0]));
			strm_seqno = repl_instance->strm_seqno[idx];
			if (strm_seqno)
			{
				PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, strm_seqno[0]) + (idx * SIZEOF(strm_seqno)),
					SIZEOF(strm_seqno));
				util_out_print( PREFIX_FILEHDR "STRM !2UL: Journal Sequence Number  !20@UQ [0x!16@XQ]", TRUE,
					idx, &strm_seqno, &strm_seqno);
			}
			assert(ARRAYSIZE(repl_instance->strm_group_info) == (MAX_SUPPL_STRMS - 1));
			/**************** Dump the "strm_group_info" structure member only if it is non-null ****************/
			if (idx)
			{
				strm_idx = idx - 1;
				strm_group_info = &repl_instance->strm_group_info[strm_idx];
				assert(SIZEOF(*strm_group_info) == SIZEOF(repl_instance->strm_group_info[0]));
				if (IS_REPL_INST_UUID_NON_NULL(*strm_group_info))
				{
					offset = offsetof(repl_inst_hdr, strm_group_info[0])
							+ (strm_idx * SIZEOF(*strm_group_info));
					PRINT_OFFSET_PREFIX(offset + offsetof(repl_inst_uuid, created_nodename[0]),
						SIZEOF(strm_group_info->created_nodename));
					/* created_nodename can contain upto MAX_NODENAME_LEN characters. If it consumes the entire
					 * array, then the last character will NOT be null terminated. Check and set the array
					 * length to be used for the call to util_out_print.
					 */
					nodename_ptr = strm_group_info->created_nodename;
					DBG_CHECK_CREATED_NODENAME(nodename_ptr);
					nodename_len = ('\0' == nodename_ptr[MAX_NODENAME_LEN - 1])
						? STRLEN((char *)nodename_ptr) : MAX_NODENAME_LEN;
					util_out_print( PREFIX_FILEHDR "STRM !2UL: Group Created Nodename       !R16AD", TRUE,
						idx, nodename_len, nodename_ptr);

					PRINT_OFFSET_PREFIX(offset + offsetof(repl_inst_uuid, this_instname[0]),
						SIZEOF(strm_group_info->this_instname));
					util_out_print( PREFIX_FILEHDR "STRM !2UL: Group Instance Name          !R16AD", TRUE,
						idx, LEN_AND_STR((char *)strm_group_info->this_instname));

					PRINT_OFFSET_PREFIX(offset + offsetof(repl_inst_uuid, creator_pid),
						SIZEOF(strm_group_info->creator_pid));
					util_out_print( PREFIX_FILEHDR "STRM !2UL: Group Creator Pid                  "
						"!10UL [0x!XL]", TRUE, idx,
						strm_group_info->creator_pid, strm_group_info->creator_pid);

					PRINT_OFFSET_PREFIX(offset + offsetof(repl_inst_uuid, created_time),
						SIZEOF(strm_group_info->created_time));
					util_out_print( PREFIX_FILEHDR "STRM !2UL: ", FALSE, idx);
					PRINT_TIME( "Group Create Time        ", strm_group_info->created_time);
				}

			}
		}
	}
	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, file_corrupt), SIZEOF(repl_instance->file_corrupt));
	PRINT_BOOLEAN(PREFIX_FILEHDR "Corrupt                                     !R10AZ", repl_instance->file_corrupt, -1);
	PRINT_OFFSET_PREFIX(offsetof(repl_inst_hdr, qdbrundown), SIZEOF(repl_instance->qdbrundown));
	PRINT_BOOLEAN(PREFIX_FILEHDR "Quick database rundown is active            !R10AZ", repl_instance->qdbrundown, -1);
}

void	repl_inst_dump_gtmsrclcl(gtmsrc_lcl_ptr_t gtmsrclcl_ptr)
{
	int		idx;
	boolean_t	first_time = TRUE;

	for (idx = 0; idx < NUM_GTMSRC_LCL; idx++, gtmsrclcl_ptr++)
	{
		if (('\0' == gtmsrclcl_ptr->secondary_instname[0])
				&& (0 == gtmsrclcl_ptr->resync_seqno)
				&& (0 == gtmsrclcl_ptr->connect_jnl_seqno))
			continue;
		if (first_time)
		{
			util_out_print("", TRUE);
			first_time = FALSE;
			PRINT_DASHES;
			util_out_print(SRCLCL_TITLE_STRING, TRUE);
			PRINT_DASHES;
		} else
			PRINT_DASHES;
		PRINT_OFFSET_HEADER;

		PRINT_OFFSET_PREFIX(offsetof(gtmsrc_lcl, secondary_instname[0]), SIZEOF(gtmsrclcl_ptr->secondary_instname));
		util_out_print( PREFIX_SRCLCL "Secondary Instance Name          !R15AD", TRUE, idx,
			LEN_AND_STR((char *)gtmsrclcl_ptr->secondary_instname));

		PRINT_OFFSET_PREFIX(offsetof(gtmsrc_lcl, resync_seqno), SIZEOF(gtmsrclcl_ptr->resync_seqno));
		util_out_print( PREFIX_SRCLCL "Resync Sequence Number      !20@UQ [0x!16@XQ]", TRUE, idx,
			&gtmsrclcl_ptr->resync_seqno, &gtmsrclcl_ptr->resync_seqno);

		PRINT_OFFSET_PREFIX(offsetof(gtmsrc_lcl, connect_jnl_seqno), SIZEOF(gtmsrclcl_ptr->connect_jnl_seqno));
		util_out_print( PREFIX_SRCLCL "Connect Sequence Number     !20@UQ [0x!16@XQ]", TRUE, idx,
			&gtmsrclcl_ptr->connect_jnl_seqno, &gtmsrclcl_ptr->connect_jnl_seqno);
		section_offset += SIZEOF(gtmsrc_lcl);
	}
}

void	repl_inst_dump_history_records(char *inst_fn, int4 num_histinfo)
{
	int4		idx, idx2, last_histnum;
	off_t		offset;
	repl_histinfo	curhistinfo;
	jnl_proc_time	whole_time;
	int		time_len;
	char		time_str[LENGTH_OF_TIME + 1];
	boolean_t	first_time = TRUE;
	unsigned char	*created_nodename;

	for (idx = 0; idx < num_histinfo; idx++, offset += SIZEOF(repl_histinfo))
	{
		if (first_time)
		{
			util_out_print("", TRUE);
			first_time = FALSE;
			PRINT_DASHES;
			util_out_print(HISTINFO_TITLE_STRING, TRUE);
			PRINT_DASHES;
			offset = REPL_INST_HISTINFO_START;
		} else
			PRINT_DASHES;
		repl_inst_read(inst_fn, (off_t)offset, (sm_uc_ptr_t)&curhistinfo, SIZEOF(repl_histinfo));
		PRINT_OFFSET_HEADER;

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, root_primary_instname[0]), SIZEOF(curhistinfo.root_primary_instname));
		util_out_print(PREFIX_HISTINFO "Root Primary Instance Name  !R15AD", TRUE, idx,
			LEN_AND_STR((char *)curhistinfo.root_primary_instname));

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, start_seqno), SIZEOF(curhistinfo.start_seqno));
		util_out_print(PREFIX_HISTINFO "Start Sequence Number  !20@UQ [0x!16@XQ]", TRUE, idx,
			&curhistinfo.start_seqno, &curhistinfo.start_seqno);

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, strm_seqno), SIZEOF(curhistinfo.strm_seqno));
		util_out_print(PREFIX_HISTINFO "Stream Sequence Number !20@UQ [0x!16@XQ]", TRUE, idx,
			&curhistinfo.strm_seqno, &curhistinfo.strm_seqno);

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, root_primary_cycle), SIZEOF(curhistinfo.root_primary_cycle));
		util_out_print(PREFIX_HISTINFO "Root Primary Cycle               !10UL [0x!XL]", TRUE, idx,
			curhistinfo.root_primary_cycle, curhistinfo.root_primary_cycle);

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, creator_pid), SIZEOF(curhistinfo.creator_pid));
		util_out_print(PREFIX_HISTINFO "Creator Process ID               !10UL [0x!XL]", TRUE, idx,
			curhistinfo.creator_pid, curhistinfo.creator_pid);

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, created_time), SIZEOF(curhistinfo.created_time));
		JNL_WHOLE_FROM_SHORT_TIME(whole_time, curhistinfo.created_time);
		time_len = format_time(whole_time, time_str, SIZEOF(time_str), SHORT_TIME_FORMAT);
		util_out_print(PREFIX_HISTINFO "Creation Time          "TIME_DISPLAY_FAO, TRUE, idx,
			time_len, time_str);

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, histinfo_num), SIZEOF(curhistinfo.histinfo_num));
		util_out_print(PREFIX_HISTINFO "History Number                   !10UL [0x!XL]", TRUE, idx,
			curhistinfo.histinfo_num, curhistinfo.histinfo_num);
		assert(curhistinfo.histinfo_num == idx);

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, prev_histinfo_num), SIZEOF(curhistinfo.prev_histinfo_num));
		util_out_print(PREFIX_HISTINFO "Previous History Number          !10SL [0x!XL]", TRUE, idx,
			curhistinfo.prev_histinfo_num, curhistinfo.prev_histinfo_num);
		assert(curhistinfo.histinfo_num == idx);

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, strm_index), SIZEOF(curhistinfo.strm_index));
		util_out_print(PREFIX_HISTINFO "Stream #                                 !2UL [0x!XL]", TRUE, idx,
			curhistinfo.strm_index, curhistinfo.strm_index);

		PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, history_type), SIZEOF(curhistinfo.history_type));
		util_out_print(PREFIX_HISTINFO "History record type                      !2UL [0x!XL]", TRUE, idx,
			curhistinfo.history_type, curhistinfo.history_type);

		/* Assert that lms_group is filled in for non-zero stream #s and not for zero stream #s */
		assert(!curhistinfo.strm_index && IS_REPL_INST_UUID_NULL(curhistinfo.lms_group)
			|| curhistinfo.strm_index && IS_REPL_INST_UUID_NON_NULL(curhistinfo.lms_group));
		/* Assert that UPDATERESYNC type of history record is possible only in non-zero stream #s */
		assert((HISTINFO_TYPE_UPDRESYNC != curhistinfo.history_type) || curhistinfo.strm_index);
		/* Do not print "lms_group" info for all history records as it will clutter the output.
		 * Print it only in case of HISTINFO_TYPE_UPDRESYNC as that is the only case when it changes.
		 * between history records belonging to the same stream #.
		 */
		if (HISTINFO_TYPE_UPDRESYNC == curhistinfo.history_type)
		{
			DBG_CHECK_CREATED_NODENAME(curhistinfo.lms_group.created_nodename);
			PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, lms_group) + offsetof(repl_inst_uuid, created_nodename[0]),
				SIZEOF(curhistinfo.lms_group.created_nodename));
			if (curhistinfo.lms_group.created_nodename[MAX_NODENAME_LEN -1])
			{
				assert(16 == MAX_NODENAME_LEN); /* because of the !R16AD hardcoding below */
				util_out_print(PREFIX_HISTINFO "LMS Group Created Nodename !R16AD", TRUE, idx,
					MAX_NODENAME_LEN, (char *)curhistinfo.lms_group.created_nodename);
			} else
				util_out_print(PREFIX_HISTINFO "LMS Group Created Nodename  !R15AD", TRUE, idx,
					LEN_AND_STR((char *)curhistinfo.lms_group.created_nodename));

			PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, lms_group) + offsetof(repl_inst_uuid, this_instname[0]),
				SIZEOF(curhistinfo.lms_group.this_instname));
			util_out_print(PREFIX_HISTINFO "LMS Group Instance Name     !R15AD", TRUE, idx,
				LEN_AND_STR((char *)curhistinfo.lms_group.this_instname));

			PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, lms_group) + offsetof(repl_inst_uuid, created_time),
				SIZEOF(curhistinfo.lms_group.created_time));
			JNL_WHOLE_FROM_SHORT_TIME(whole_time, curhistinfo.lms_group.created_time);
			time_len = format_time(whole_time, time_str, SIZEOF(time_str), SHORT_TIME_FORMAT);
			util_out_print(PREFIX_HISTINFO "LMS Group Creation Time"TIME_DISPLAY_FAO, TRUE, idx,
				time_len, time_str);

			PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, lms_group) + offsetof(repl_inst_uuid, creator_pid),
				SIZEOF(curhistinfo.lms_group.creator_pid));
			util_out_print(PREFIX_HISTINFO "LMS Group Creator PID            !10UL [0x!XL]", TRUE, idx,
				curhistinfo.lms_group.creator_pid, curhistinfo.lms_group.creator_pid);
		}
		for (idx2 = 0; idx2 < MAX_SUPPL_STRMS; idx2++)
		{
			last_histnum = curhistinfo.last_histinfo_num[idx2];
			assert(SIZEOF(last_histnum) == SIZEOF(curhistinfo.last_histinfo_num[idx2]));
			if (INVALID_HISTINFO_NUM != last_histnum)
			{
				PRINT_OFFSET_PREFIX(offsetof(repl_histinfo, last_histinfo_num[0]) + (idx2 * SIZEOF(last_histnum)),
					SIZEOF(last_histnum));
				util_out_print(PREFIX_HISTINFO "Stream !2UL: Last History Number   !10UL [0x!XL]", TRUE, idx,
					idx2, last_histnum, last_histnum);
			}
		}
		section_offset += SIZEOF(repl_histinfo);
	}
}

void	repl_dump_histinfo(FILE *log_fp, boolean_t stamptime, boolean_t flush, char *start_text, repl_histinfo *cur_histinfo)
{
	unsigned char		nodename_buff[MAX_NODENAME_LEN + 1], *created_nodename;

	repl_log(log_fp, stamptime, flush, "%s : Start Seqno = %llu [0x%llx] : Stream Seqno = %llu [0x%llx] : "
		"Root Primary = [%s] : Cycle = [%d] : Creator pid = %d : Created time = %d [0x%x] : History number = %d : "
		"Prev History number = %d : Stream # = %d : History type = %d\n", start_text,
		cur_histinfo->start_seqno, cur_histinfo->start_seqno, cur_histinfo->strm_seqno, cur_histinfo->strm_seqno,
		cur_histinfo->root_primary_instname, cur_histinfo->root_primary_cycle, cur_histinfo->creator_pid,
		cur_histinfo->created_time, cur_histinfo->created_time, cur_histinfo->histinfo_num,
		cur_histinfo->prev_histinfo_num, cur_histinfo->strm_index, cur_histinfo->history_type);
	/* Assert that lms_group is filled in for non-zero stream #s and not for zero stream #s */
	assert(!cur_histinfo->strm_index && IS_REPL_INST_UUID_NULL(cur_histinfo->lms_group)
		|| cur_histinfo->strm_index && IS_REPL_INST_UUID_NON_NULL(cur_histinfo->lms_group));
	/* Assert that UPDATERESYNC type of history record is possible only in non-zero stream #s */
	assert((HISTINFO_TYPE_UPDRESYNC != cur_histinfo->history_type) || cur_histinfo->strm_index);
	/* Do not print "lms_group" info for all history records as it will clutter the output.
	 * Print it only in case of HISTINFO_TYPE_UPDRESYNC as that is the only case when it changes
	 * between history records belonging to the same stream #.
	 */
	if (HISTINFO_TYPE_UPDRESYNC == cur_histinfo->history_type)
	{	/* history record that also contains NEW lms group info (due to -UPDATERESYNC) */
		DBG_CHECK_CREATED_NODENAME(cur_histinfo->lms_group.created_nodename);
		assert(MAX_NODENAME_LEN == SIZEOF(cur_histinfo->lms_group.created_nodename));
		if ('\0' == cur_histinfo->lms_group.created_nodename[MAX_NODENAME_LEN - 1])
			created_nodename = &cur_histinfo->lms_group.created_nodename[0];
		else
		{
			created_nodename = &nodename_buff[0];
			memcpy(created_nodename, cur_histinfo->lms_group.created_nodename, MAX_NODENAME_LEN);
			created_nodename[MAX_NODENAME_LEN] = '\0';
		}
		repl_log(log_fp, stamptime, flush, "History has non-zero Supplementary Stream LMS Group Content : "
			"LMS Group Nodename = [%s] : LMS Group Instance Name = [%s] : "
			"Creator pid = %d : Created time = %d [0x%x]\n",
			created_nodename, cur_histinfo->lms_group.this_instname,
			cur_histinfo->lms_group.creator_pid,
			cur_histinfo->lms_group.created_time, cur_histinfo->lms_group.created_time);
	}
	/* Do not print "last_histinfo_num" output as that is not of concern to the user and only clutters the output */
}

void	repl_inst_dump_jnlpoolctl(jnlpool_ctl_ptr_t jnlpool_ctl)
{
	char			*string;
	repl_conn_info_t	*this_side;
	int			idx;
	seq_num			strm_seqno;

	util_out_print("", TRUE);
	PRINT_DASHES;
	util_out_print(JNLPOOLCTL_TITLE_STRING, TRUE);
	PRINT_DASHES;
	PRINT_OFFSET_HEADER;

	assert(0 == offsetof(jnlpool_ctl_struct, jnlpool_id));	/* The following offsetof calculations depend on this */
	PRINT_OFFSET_PREFIX(offsetof(replpool_identifier, label[0]), SIZEOF(jnlpool_ctl->jnlpool_id.label));
	util_out_print( PREFIX_JNLPOOLCTL "Label                                      !11AD", TRUE,
		GDS_LABEL_SZ - 1, jnlpool_ctl->jnlpool_id.label);

	PRINT_OFFSET_PREFIX(offsetof(replpool_identifier, pool_type), SIZEOF(jnlpool_ctl->jnlpool_id.pool_type));
	string = (JNLPOOL_SEGMENT == jnlpool_ctl->jnlpool_id.pool_type) ? "JNLPOOL" :
			((RECVPOOL_SEGMENT == jnlpool_ctl->jnlpool_id.pool_type) ? "RECVPOOL" : "UNKNOWN");
	if (MEMCMP_LIT(string, "UNKNOWN"))
		util_out_print( PREFIX_JNLPOOLCTL "Type                            !R22AZ", TRUE, string);
	else
	{
		util_out_print( PREFIX_JNLPOOLCTL "Type                            !R22AZ [0x!XL]", TRUE,
			string, jnlpool_ctl->jnlpool_id.pool_type);
	}

	PRINT_OFFSET_PREFIX(offsetof(replpool_identifier, now_running[0]), SIZEOF(jnlpool_ctl->jnlpool_id.now_running));
	util_out_print( PREFIX_JNLPOOLCTL "YottaDB Version         !R30AZ", TRUE, jnlpool_ctl->jnlpool_id.now_running);

	PRINT_OFFSET_PREFIX(offsetof(replpool_identifier, instfilename[0]), SIZEOF(jnlpool_ctl->jnlpool_id.instfilename));
	if (22 >= strlen(jnlpool_ctl->jnlpool_id.instfilename))
	{
		util_out_print( PREFIX_JNLPOOLCTL "Instance file name              !R22AZ",
			TRUE, jnlpool_ctl->jnlpool_id.instfilename);
	} else
		util_out_print( PREFIX_JNLPOOLCTL "Instance file name      !AZ", TRUE, jnlpool_ctl->jnlpool_id.instfilename);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, start_jnl_seqno), SIZEOF(jnlpool_ctl->start_jnl_seqno));
	util_out_print( PREFIX_JNLPOOLCTL "Start Journal Seqno               !20@UQ [0x!16@XQ]", TRUE,
		&jnlpool_ctl->start_jnl_seqno, &jnlpool_ctl->start_jnl_seqno);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, jnl_seqno), SIZEOF(jnlpool_ctl->jnl_seqno));
	util_out_print( PREFIX_JNLPOOLCTL "Journal Seqno                     !20@UQ [0x!16@XQ]", TRUE,
		&jnlpool_ctl->jnl_seqno, &jnlpool_ctl->jnl_seqno);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, last_histinfo_seqno), SIZEOF(jnlpool_ctl->last_histinfo_seqno));
	util_out_print( PREFIX_JNLPOOLCTL "Last histinfo Seqno               !20@UQ [0x!16@XQ]", TRUE,
		&jnlpool_ctl->last_histinfo_seqno, &jnlpool_ctl->last_histinfo_seqno);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, jnldata_base_off), SIZEOF(jnlpool_ctl->jnldata_base_off));
	util_out_print( PREFIX_JNLPOOLCTL "Journal Data Base Offset                    !10UL [0x!XL]", TRUE,
		jnlpool_ctl->jnldata_base_off, jnlpool_ctl->jnldata_base_off);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, jnlpool_size), SIZEOF(jnlpool_ctl->jnlpool_size));
	util_out_print( PREFIX_JNLPOOLCTL "Journal Pool Size (in bytes)                !10UL [0x!XL]", TRUE,
		jnlpool_ctl->jnlpool_size, jnlpool_ctl->jnlpool_size);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, rsrv_write_addr), SIZEOF(jnlpool_ctl->rsrv_write_addr));
	util_out_print( PREFIX_JNLPOOLCTL "Reserved Write Offset             !20@UQ [0x!16@XQ]", TRUE,
		&jnlpool_ctl->rsrv_write_addr, &jnlpool_ctl->rsrv_write_addr);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, write_addr), SIZEOF(jnlpool_ctl->write_addr));
	util_out_print( PREFIX_JNLPOOLCTL "Absolute Write Offset             !20@UQ [0x!16@XQ]", TRUE,
		&jnlpool_ctl->write_addr, &jnlpool_ctl->write_addr);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, upd_disabled), SIZEOF(jnlpool_ctl->upd_disabled));
	PRINT_BOOLEAN(PREFIX_JNLPOOLCTL "Updates Disabled                !R22AZ", jnlpool_ctl->upd_disabled, -1);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, lastwrite_len), SIZEOF(jnlpool_ctl->lastwrite_len));
	util_out_print( PREFIX_JNLPOOLCTL "Last Write Length (in bytes)                !10UL [0x!XL]", TRUE,
		jnlpool_ctl->lastwrite_len, jnlpool_ctl->lastwrite_len);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, send_losttn_complete), SIZEOF(jnlpool_ctl->send_losttn_complete));
	PRINT_BOOLEAN(PREFIX_JNLPOOLCTL "Send LostTN Complete            !R22AZ", jnlpool_ctl->send_losttn_complete, -1);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, primary_instname[0]), SIZEOF(jnlpool_ctl->primary_instname));
	util_out_print( PREFIX_JNLPOOLCTL "Primary Instance Name             !R20AZ",
		TRUE, jnlpool_ctl->primary_instname);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, max_zqgblmod_seqno), SIZEOF(jnlpool_ctl->max_zqgblmod_seqno));
	util_out_print( PREFIX_JNLPOOLCTL "Zqgblmod Seqno                    !20@UQ [0x!16@XQ]", TRUE,
		&jnlpool_ctl->max_zqgblmod_seqno, &jnlpool_ctl->max_zqgblmod_seqno);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, prev_jnlseqno_time), SIZEOF(jnlpool_ctl->prev_jnlseqno_time));
	PRINT_TIME( PREFIX_JNLPOOLCTL "Prev JnlSeqno Time                ", jnlpool_ctl->prev_jnlseqno_time);

	/**************** Dump the "this_side" structure member ****************/
	this_side = &jnlpool_ctl->this_side;
	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, this_side) + offsetof(repl_conn_info_t, proto_ver),
				SIZEOF(this_side->proto_ver));
	util_out_print( PREFIX_JNLPOOLCTL "Protocol Version                                   !3UL [0x!XL]", TRUE,
			this_side->proto_ver, this_side->proto_ver);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, this_side) + offsetof(repl_conn_info_t, jnl_ver),
				SIZEOF(this_side->jnl_ver));
	util_out_print( PREFIX_JNLPOOLCTL "Journal Version                                    !3UL [0x!XL]", TRUE,
			this_side->jnl_ver, this_side->jnl_ver);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, this_side) + offsetof(repl_conn_info_t, is_std_null_coll),
				SIZEOF(this_side->is_std_null_coll));
	PRINT_BOOLEAN(PREFIX_JNLPOOLCTL "Standard Null Collation         !R22AZ", this_side->is_std_null_coll, -1);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, this_side) + offsetof(repl_conn_info_t, trigger_supported),
				SIZEOF(this_side->trigger_supported));
	PRINT_BOOLEAN(PREFIX_JNLPOOLCTL "Trigger Supported               !R22AZ", this_side->trigger_supported, -1);

	/* The following 3 members of "this_side" don't make sense as the structure reflects properties of this instance whereas
	 * these 3 members require connection with the remote side in order to make sense. So skip dumping them.
	 *	this_side->cross_endian
	 *	this_side->endianness_known
	 *	this_side->null_subs_xform
	 */

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, this_side) + offsetof(repl_conn_info_t, is_supplementary),
				SIZEOF(this_side->is_supplementary));
	PRINT_BOOLEAN(PREFIX_JNLPOOLCTL "Supplementary Instance          !R22AZ", this_side->is_supplementary, -1);
	/**************** Dump the "strm_seqno" structure member if this is a supplementary instance ****************/
	if (this_side->is_supplementary)
	{
		assert(SIZEOF(strm_seqno) == SIZEOF(jnlpool_ctl->strm_seqno[0]));
		assert(MAX_SUPPL_STRMS == ARRAYSIZE(jnlpool_ctl->strm_seqno));
		for (idx = 0; idx < MAX_SUPPL_STRMS; idx++)
		{
			strm_seqno = jnlpool_ctl->strm_seqno[idx];
			if (strm_seqno)
			{
				PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, strm_seqno[0]) + idx * SIZEOF(strm_seqno),
					SIZEOF(strm_seqno));
				util_out_print( PREFIX_JNLPOOLCTL "Stream !2UL: Journal Seqno          !20@UQ [0x!16@XQ]", TRUE,
					idx, &strm_seqno, &strm_seqno);
			}
		}
	}
	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, onln_rlbk_pid), SIZEOF(jnlpool_ctl->onln_rlbk_pid));
	util_out_print( PREFIX_JNLPOOLCTL "Online Rollback PID               !20UL [0x!XL]", TRUE,
		jnlpool_ctl->onln_rlbk_pid, jnlpool_ctl->onln_rlbk_pid);

	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, onln_rlbk_cycle), SIZEOF(jnlpool_ctl->onln_rlbk_cycle));
	util_out_print( PREFIX_JNLPOOLCTL "Online Rollback Cycle             !20UL [0x!XL]", TRUE,
		jnlpool_ctl->onln_rlbk_cycle, jnlpool_ctl->onln_rlbk_cycle);
	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, freeze), SIZEOF(jnlpool_ctl->freeze));
	PRINT_BOOLEAN( PREFIX_JNLPOOLCTL  "Freeze                                      !R10AZ", jnlpool_ctl->freeze, -1);
	if (jnlpool_ctl->freeze)
	{
		PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, freeze_comment[0]), SIZEOF(jnlpool_ctl->freeze_comment));
		if (STRLEN(jnlpool_ctl->freeze_comment) <= 38)
			util_out_print( PREFIX_JNLPOOLCTL "Freeze Comment  !R38AZ", TRUE, jnlpool_ctl->freeze_comment);
		else
			util_out_print( PREFIX_JNLPOOLCTL "Freeze Comment: !AZ", TRUE, jnlpool_ctl->freeze_comment);
	}
	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, instfreeze_environ_inited),
			SIZEOF(jnlpool_ctl->instfreeze_environ_inited));
	PRINT_BOOLEAN( PREFIX_JNLPOOLCTL  "Custom Errors Loaded                        !R10AZ",
			jnlpool_ctl->instfreeze_environ_inited, -1);
	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, ftok_counter_halted), SIZEOF(jnlpool_ctl->ftok_counter_halted));
	PRINT_BOOLEAN( PREFIX_JNLPOOLCTL  "FTOK Counter Halted                         !R10AZ",
									jnlpool_ctl->ftok_counter_halted, -1);
	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, phase2_commit_index1), SIZEOF(jnlpool_ctl->phase2_commit_index1));
	util_out_print( PREFIX_JNLPOOLCTL "Phase2 Commit Index1                        !10UL [0x!XL]", TRUE,
		jnlpool_ctl->phase2_commit_index1, jnlpool_ctl->phase2_commit_index1);
	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, phase2_commit_index2), SIZEOF(jnlpool_ctl->phase2_commit_index2));
	util_out_print( PREFIX_JNLPOOLCTL "Phase2 Commit Index2                        !10UL [0x!XL]", TRUE,
		jnlpool_ctl->phase2_commit_index2, jnlpool_ctl->phase2_commit_index2);
	PRINT_OFFSET_PREFIX(0, 0);
	util_out_print( PREFIX_JNLPOOLCTL "Phase2 Commit IndexMax                      !10UL [0x!XL]", TRUE,
		JPL_PHASE2_COMMIT_ARRAY_SIZE, JPL_PHASE2_COMMIT_ARRAY_SIZE);
	PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, phase2_commit_latch), SIZEOF(jnlpool_ctl->phase2_commit_latch));
	util_out_print( PREFIX_JNLPOOLCTL "Phase2 Commit Latch Pid                     !10UL [0x!XL]", TRUE,
		jnlpool_ctl->phase2_commit_latch.u.parts.latch_pid, jnlpool_ctl->phase2_commit_latch.u.parts.latch_pid);
#	define TAB_JPL_TRC_REC(A,B)										\
	{													\
		PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, B.cntr), SIZEOF(jnlpool_ctl->B.cntr));		\
		util_out_print( PREFIX_JNLPOOLCTL A " Cntr               !10UL [0x!XL]", TRUE,			\
			jnlpool_ctl->B.cntr, jnlpool_ctl->B.cntr);						\
		PRINT_OFFSET_PREFIX(offsetof(jnlpool_ctl_struct, B.seqno), SIZEOF(jnlpool_ctl->B.seqno));	\
		util_out_print( PREFIX_JNLPOOLCTL A " Seqno              !10UL [0x!XL]", TRUE,			\
			jnlpool_ctl->B.seqno, jnlpool_ctl->B.seqno);						\
	}
#	include "tab_jpl_trc_rec.h"
#	undef TAB_JPL_TRC_REC
}

void	repl_inst_dump_gtmsourcelocal(gtmsource_local_ptr_t gtmsourcelocal_ptr)
{
	int			idx, idx2;
	char			*string;
	boolean_t		first_time = TRUE;
	repl_conn_info_t	*remote_side;
	int			errcode;
	char			secondary_addr[SA_MAXLEN + 1];

	for (idx = 0; idx < NUM_GTMSRC_LCL; idx++, gtmsourcelocal_ptr++)
	{
		if (('\0' == gtmsourcelocal_ptr->secondary_instname[0])
				&& (0 == gtmsourcelocal_ptr->read_jnl_seqno)
				&& (0 == gtmsourcelocal_ptr->connect_jnl_seqno))
			continue;
		if (first_time)
		{
			util_out_print("", TRUE);
			first_time = FALSE;
			PRINT_DASHES;
			util_out_print(SOURCELOCAL_TITLE_STRING, TRUE);
			PRINT_DASHES;
		} else
			PRINT_DASHES;
		PRINT_OFFSET_HEADER;

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, secondary_instname[0]),
			SIZEOF(gtmsourcelocal_ptr->secondary_instname));
		util_out_print( PREFIX_SOURCELOCAL "Secondary Instance Name         !R16AZ", TRUE, idx,
			(char *)gtmsourcelocal_ptr->secondary_instname);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, gtmsource_pid), SIZEOF(gtmsourcelocal_ptr->gtmsource_pid));
		util_out_print( PREFIX_SOURCELOCAL "Source Server Pid                     !10UL", TRUE, idx,
			gtmsourcelocal_ptr->gtmsource_pid);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, mode), SIZEOF(gtmsourcelocal_ptr->mode));
		if (GTMSOURCE_MODE_ACTIVE == gtmsourcelocal_ptr->mode)
			string = "ACTIVE";
		else if (GTMSOURCE_MODE_PASSIVE == gtmsourcelocal_ptr->mode)
			string = "PASSIVE";
		else if (GTMSOURCE_MODE_ACTIVE_REQUESTED == gtmsourcelocal_ptr->mode)
			string = "ACTIVE REQUESTED";
		else if (GTMSOURCE_MODE_PASSIVE_REQUESTED == gtmsourcelocal_ptr->mode)
			string = "PASSIVE REQUESTED";
		else
			string = "UNKNOWN";
		if (MEMCMP_LIT(string, "UNKNOWN"))
			util_out_print( PREFIX_SOURCELOCAL "Source Server Mode        !R22AZ", TRUE, idx, string);
		else
		{
			util_out_print( PREFIX_SOURCELOCAL "Source Server Mode        !R22AZ [0x!XL]", TRUE, idx,
				string, gtmsourcelocal_ptr->mode);
		}

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, gtmsource_state), SIZEOF(gtmsourcelocal_ptr->gtmsource_state));
		string = ((0 <= (signed)gtmsourcelocal_ptr->gtmsource_state)
				&& (GTMSOURCE_NUM_STATES > (signed)gtmsourcelocal_ptr->gtmsource_state))
					? (char *)state_array[gtmsourcelocal_ptr->gtmsource_state] : "UNKNOWN";
		if (MEMCMP_LIT(string, "UNKNOWN"))
			util_out_print( PREFIX_SOURCELOCAL "Processing State          !R22AZ", TRUE, idx, string);
		else
		{
			util_out_print( PREFIX_SOURCELOCAL "Processing State          !R22AZ [0x!XL]", TRUE, idx,
				string, gtmsourcelocal_ptr->gtmsource_state);
		}

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, gtmsrc_lcl_array_index),
			SIZEOF(gtmsourcelocal_ptr->gtmsrc_lcl_array_index));
		util_out_print( PREFIX_SOURCELOCAL "Slot Index                       !15UL", TRUE, idx,
			gtmsourcelocal_ptr->gtmsrc_lcl_array_index);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, repl_zlib_cmp_level),
			SIZEOF(gtmsourcelocal_ptr->repl_zlib_cmp_level));
		util_out_print( PREFIX_SOURCELOCAL "Journal record Compression Level !15UL", TRUE, idx,
			gtmsourcelocal_ptr->repl_zlib_cmp_level);

		/**************** Dump the "remote_side" structure member ****************/
		remote_side = &gtmsourcelocal_ptr->remote_side;
		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, remote_side) + offsetof(repl_conn_info_t, proto_ver),
					SIZEOF(remote_side->proto_ver));
		util_out_print( PREFIX_SOURCELOCAL "Remote Protocol Version                      !3SL [0x!XL]", TRUE,
				idx, remote_side->proto_ver, remote_side->proto_ver);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, remote_side) + offsetof(repl_conn_info_t, jnl_ver),
					SIZEOF(remote_side->jnl_ver));
		util_out_print( PREFIX_SOURCELOCAL "Remote Journal Version                       !3UL [0x!XL]", TRUE,
				idx, remote_side->jnl_ver, remote_side->jnl_ver);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, remote_side) + offsetof(repl_conn_info_t, is_std_null_coll),
					SIZEOF(remote_side->is_std_null_coll));
		PRINT_BOOLEAN(PREFIX_SOURCELOCAL "Remote has Standard Null Collation  !R12AZ", remote_side->is_std_null_coll, idx);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, remote_side) + offsetof(repl_conn_info_t, trigger_supported),
					SIZEOF(remote_side->trigger_supported));
		PRINT_BOOLEAN(PREFIX_SOURCELOCAL "Remote Supports Triggers            !R12AZ", remote_side->trigger_supported, idx);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, remote_side) + offsetof(repl_conn_info_t, cross_endian),
					SIZEOF(remote_side->cross_endian));
		PRINT_BOOLEAN(PREFIX_SOURCELOCAL "Remote is Cross Endian              !R12AZ", remote_side->cross_endian, idx);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, remote_side) + offsetof(repl_conn_info_t, endianness_known),
					SIZEOF(remote_side->endianness_known));
		PRINT_BOOLEAN(PREFIX_SOURCELOCAL "Remote Endianness Known             !R12AZ", remote_side->endianness_known, idx);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, remote_side) + offsetof(repl_conn_info_t, null_subs_xform),
					SIZEOF(remote_side->null_subs_xform));
		PRINT_BOOLEAN(PREFIX_SOURCELOCAL "Remote needs Null Subs Xform        !R12AZ", remote_side->null_subs_xform, idx);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, remote_side) + offsetof(repl_conn_info_t, is_supplementary),
					SIZEOF(remote_side->is_supplementary));
		PRINT_BOOLEAN(PREFIX_SOURCELOCAL "Remote is Supplementary Instance    !R12AZ", remote_side->is_supplementary, idx);
		/**************** "remote_side" structure member dump done ****************/

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, read_state), SIZEOF(gtmsourcelocal_ptr->read_state));
		string = (READ_POOL == gtmsourcelocal_ptr->read_state) ? "POOL" :
				((READ_FILE == gtmsourcelocal_ptr->read_state) ? "FILE" : "UNKNOWN");
		if (STRNCMP_LIT(string, "UNKNOWN"))
			util_out_print( PREFIX_SOURCELOCAL "Currently Reading from     !R21AZ", TRUE, idx, string);
		else
		{
			util_out_print( PREFIX_SOURCELOCAL "Currently Reading from     !R21AZ [0x!XL]", TRUE, idx,
				string, gtmsourcelocal_ptr->read_state);
		}

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, jnlfileonly), SIZEOF(gtmsourcelocal_ptr->jnlfileonly));
		PRINT_BOOLEAN(PREFIX_SOURCELOCAL "Journal File Only                   !R12AZ",
				gtmsourcelocal_ptr->jnlfileonly, idx);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, read), SIZEOF(gtmsourcelocal_ptr->read));
		util_out_print( PREFIX_SOURCELOCAL "Relative Read Offset                  !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->read, gtmsourcelocal_ptr->read);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, read_addr), SIZEOF(gtmsourcelocal_ptr->read_addr));
		util_out_print( PREFIX_SOURCELOCAL "Absolute Read Offset        !20@UQ [0x!16@XQ]", TRUE, idx,
			&gtmsourcelocal_ptr->read_addr, &gtmsourcelocal_ptr->read_addr);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, read_jnl_seqno), SIZEOF(gtmsourcelocal_ptr->read_jnl_seqno));
		util_out_print( PREFIX_SOURCELOCAL "Resync Sequence Number      !20@UQ [0x!16@XQ]", TRUE, idx,
			&gtmsourcelocal_ptr->read_jnl_seqno, &gtmsourcelocal_ptr->read_jnl_seqno);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, connect_jnl_seqno),
			SIZEOF(gtmsourcelocal_ptr->connect_jnl_seqno));
		util_out_print( PREFIX_SOURCELOCAL "Connect Sequence Number     !20@UQ [0x!16@XQ]", TRUE, idx,
			&gtmsourcelocal_ptr->connect_jnl_seqno, &gtmsourcelocal_ptr->connect_jnl_seqno);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, num_histinfo), SIZEOF(gtmsourcelocal_ptr->num_histinfo));
		util_out_print( PREFIX_SOURCELOCAL "Number of histinfo structures         !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->num_histinfo, gtmsourcelocal_ptr->num_histinfo);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, next_histinfo_num),
			SIZEOF(gtmsourcelocal_ptr->next_histinfo_num));
		util_out_print( PREFIX_SOURCELOCAL "Next histinfo Number                  !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->next_histinfo_num, gtmsourcelocal_ptr->next_histinfo_num);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, next_histinfo_seqno),
			SIZEOF(gtmsourcelocal_ptr->next_histinfo_seqno));
		util_out_print( PREFIX_SOURCELOCAL "Next histinfo Seqno         !20@UQ [0x!16@XQ]", TRUE, idx,
			&gtmsourcelocal_ptr->next_histinfo_seqno, &gtmsourcelocal_ptr->next_histinfo_seqno);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, last_flush_resync_seqno),
			SIZEOF(gtmsourcelocal_ptr->last_flush_resync_seqno));
		util_out_print( PREFIX_SOURCELOCAL "Last Flush Resync Seqno     !20@UQ [0x!16@XQ]", TRUE, idx,
			&gtmsourcelocal_ptr->last_flush_resync_seqno, &gtmsourcelocal_ptr->last_flush_resync_seqno);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, send_new_histrec),
			SIZEOF(gtmsourcelocal_ptr->send_new_histrec));
		PRINT_BOOLEAN( PREFIX_SOURCELOCAL "Send New histinfo                       !R8AZ",
			gtmsourcelocal_ptr->send_new_histrec, idx);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, send_losttn_complete),
			SIZEOF(gtmsourcelocal_ptr->send_losttn_complete));
		PRINT_BOOLEAN( PREFIX_SOURCELOCAL "Send LostTN Complete                    !R8AZ",
			gtmsourcelocal_ptr->send_losttn_complete, idx);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, secondary_host[0]),
			SIZEOF(gtmsourcelocal_ptr->secondary_host));
		if (20 >= strlen(gtmsourcelocal_ptr->secondary_host))
		{
			util_out_print( PREFIX_SOURCELOCAL "Secondary HOSTNAME          !R20AZ",
				TRUE, idx, gtmsourcelocal_ptr->secondary_host);
		} else
		{
			util_out_print( PREFIX_SOURCELOCAL "Secondary HOSTNAME          !AZ",
				TRUE, idx, gtmsourcelocal_ptr->secondary_host);
		}
		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, secondary_af),
			SIZEOF(gtmsourcelocal_ptr->secondary_af));
		string = (AF_INET == gtmsourcelocal_ptr->secondary_af)?  "IPv4" :
				((AF_INET6 == gtmsourcelocal_ptr->secondary_af) ? "IPv6" : "UNKNOWN");
		util_out_print( PREFIX_SOURCELOCAL "Secondary Address Family              !R10AZ",TRUE, idx, string);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, secondary_inet_addr),
			SIZEOF(gtmsourcelocal_ptr->secondary_inet_addr));
		errcode = getnameinfo((struct sockaddr *)&gtmsourcelocal_ptr->secondary_inet_addr,
					gtmsourcelocal_ptr->secondary_addrlen, secondary_addr, SA_MAXLEN, NULL, 0, NI_NUMERICHOST);
		if (0 == errcode)
		{
			util_out_print( PREFIX_SOURCELOCAL "Secondary INET Address  !R24AZ", TRUE, idx, secondary_addr);
		} else
		{
			string = "UNKNOWN";
			util_out_print( PREFIX_SOURCELOCAL "Secondary INET Address                !R10AZ", TRUE, idx, string);
		}

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, secondary_port), SIZEOF(gtmsourcelocal_ptr->secondary_port));
		util_out_print( PREFIX_SOURCELOCAL "Secondary Port                        !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->secondary_port, gtmsourcelocal_ptr->secondary_port);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, log_interval), SIZEOF(gtmsourcelocal_ptr->log_interval));
		util_out_print( PREFIX_SOURCELOCAL "Log Interval                          !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->log_interval, gtmsourcelocal_ptr->log_interval);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, log_file[0]), SIZEOF(gtmsourcelocal_ptr->log_file));
		if (20 >= strlen(gtmsourcelocal_ptr->log_file))
			util_out_print( PREFIX_SOURCELOCAL "Log File                    !R20AZ",
				TRUE, idx, gtmsourcelocal_ptr->log_file);
		else
			util_out_print( PREFIX_SOURCELOCAL "Log File                    !AZ",
				TRUE, idx, gtmsourcelocal_ptr->log_file);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, changelog), SIZEOF(gtmsourcelocal_ptr->changelog));
		util_out_print( PREFIX_SOURCELOCAL "Changelog                             !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->changelog, gtmsourcelocal_ptr->changelog);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, statslog), SIZEOF(gtmsourcelocal_ptr->statslog));
		util_out_print( PREFIX_SOURCELOCAL "Statslog                              !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->statslog, gtmsourcelocal_ptr->statslog);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, statslog_file[0]), SIZEOF(gtmsourcelocal_ptr->statslog_file));
		if (20 >= strlen(gtmsourcelocal_ptr->statslog_file))
		{
			util_out_print( PREFIX_SOURCELOCAL "Statslog File               !R20AZ",
				TRUE, idx, gtmsourcelocal_ptr->statslog_file);
		} else
		{	/*After gtm-7296, the statslog_file length should always be 0, so the following in fact won't be executed*/
			util_out_print( PREFIX_SOURCELOCAL "Statslog File               !AZ",
				TRUE, idx, gtmsourcelocal_ptr->statslog_file);
		}

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, connect_parms[GTMSOURCE_CONN_HARD_TRIES_COUNT]),
			SIZEOF(gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HARD_TRIES_COUNT]));
		util_out_print( PREFIX_SOURCELOCAL "Connect Parms Hard Tries Count        !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HARD_TRIES_COUNT],
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HARD_TRIES_COUNT]);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, connect_parms[GTMSOURCE_CONN_HARD_TRIES_PERIOD]),
			SIZEOF(gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HARD_TRIES_PERIOD]));
		util_out_print( PREFIX_SOURCELOCAL "Connect Parms Hard Tries Period       !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HARD_TRIES_PERIOD],
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HARD_TRIES_PERIOD]);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, connect_parms[GTMSOURCE_CONN_SOFT_TRIES_PERIOD]),
			SIZEOF(gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_SOFT_TRIES_PERIOD]));
		util_out_print( PREFIX_SOURCELOCAL "Connect Parms Soft Tries Period       !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_SOFT_TRIES_PERIOD],
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_SOFT_TRIES_PERIOD]);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, connect_parms[GTMSOURCE_CONN_ALERT_PERIOD]),
			SIZEOF(gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_ALERT_PERIOD]));
		util_out_print( PREFIX_SOURCELOCAL "Connect Parms Alert Period            !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_ALERT_PERIOD],
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_ALERT_PERIOD]);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, connect_parms[GTMSOURCE_CONN_HEARTBEAT_PERIOD]),
			SIZEOF(gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HEARTBEAT_PERIOD]));
		util_out_print( PREFIX_SOURCELOCAL "Connect Parms Heartbeat Period        !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HEARTBEAT_PERIOD],
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HEARTBEAT_PERIOD]);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, connect_parms[GTMSOURCE_CONN_HEARTBEAT_MAX_WAIT]),
			SIZEOF(gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HEARTBEAT_MAX_WAIT]));
		util_out_print( PREFIX_SOURCELOCAL "Connect Parms Heartbeat Max Wait      !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HEARTBEAT_MAX_WAIT],
			gtmsourcelocal_ptr->connect_parms[GTMSOURCE_CONN_HEARTBEAT_MAX_WAIT]);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, shutdown), SIZEOF(gtmsourcelocal_ptr->shutdown));
		util_out_print( PREFIX_SOURCELOCAL "Shutdown State                        !10UL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->shutdown, gtmsourcelocal_ptr->shutdown);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, shutdown_time), SIZEOF(gtmsourcelocal_ptr->shutdown_time));
		util_out_print( PREFIX_SOURCELOCAL "Shutdown Time in seconds              !10SL [0x!XL]", TRUE, idx,
			gtmsourcelocal_ptr->shutdown_time, gtmsourcelocal_ptr->shutdown_time);

		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, filter_cmd[0]), SIZEOF(gtmsourcelocal_ptr->filter_cmd));
		if (20 >= strlen(gtmsourcelocal_ptr->filter_cmd))
		{
			util_out_print( PREFIX_SOURCELOCAL "Filter Command              !R20AZ",
				TRUE, idx, gtmsourcelocal_ptr->filter_cmd);
		} else
		{
			util_out_print( PREFIX_SOURCELOCAL "Filter Command              !AZ",
				TRUE, idx, gtmsourcelocal_ptr->filter_cmd);
		}
		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, next_renegotiate_time),
					SIZEOF(gtmsourcelocal_ptr->next_renegotiate_time));
		util_out_print(PREFIX_SOURCELOCAL, FALSE, idx);
		PRINT_TIME("Next Renegotiate Time       ", gtmsourcelocal_ptr->next_renegotiate_time);
		PRINT_OFFSET_PREFIX(offsetof(gtmsource_local_struct, num_renegotiations),
					SIZEOF(gtmsourcelocal_ptr->num_renegotiations));
		util_out_print(PREFIX_SOURCELOCAL "Number of TLS/SSL renegotiations      !10UL [0x!XL]", TRUE, idx,
					gtmsourcelocal_ptr->num_renegotiations, gtmsourcelocal_ptr->num_renegotiations);
		PRINT_DASHES;
	}
}
