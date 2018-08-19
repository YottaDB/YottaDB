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
#include "gtm_stdio.h"
#include "gtm_string.h"
#include "gtm_unistd.h"
#include "gtm_stat.h"
#include "gtm_iconv.h"

#include <errno.h>
#include <sys/ioctl.h>

#include "io.h"
#include "iosp.h"
#include "io_params.h"
#include "cryptdef.h"
#include "iotimer.h"
#include "gt_timer.h"
#include "copy.h"
#include "iottdef.h"
#include "iormdef.h"
#include "mupip_io_dev_dispatch.h"
#include "eintr_wrappers.h"
#include "mmemory.h"
#include "mu_op_open.h"
#include "trans_log_name.h"
#include "gtm_conv.h"
#include "gtm_utf8.h"
#include "gtmimagename.h"

#define  LOGNAME_LEN 255

GBLDEF	io_desc			*active_device;

GBLREF bool			licensed;
GBLREF dev_dispatch_struct  	io_dev_dispatch_mupip[];
GBLREF int4			lkid,lid;
GBLREF	mstr			sys_input, sys_output;

error_def(LP_NOTACQ);			/* bad license 		  */
error_def(ERR_LOGTOOLONG);
error_def(ERR_SYSCALL);

LITREF mstr			chset_names[];
LITREF	unsigned char		io_params_size[];

static boolean_t mu_open_try(io_log_name *, io_log_name *, mval *, mval *);

/*	The third parameter is dummy to keep the inteface same as op_open	*/
int mu_op_open(mval *v, mval *p, mval *t, mval *mspace)
{
	char		buf1[MAX_TRANS_NAME_LEN]; /* buffer to hold translated name */
	int4		stat;		/* status */
	io_log_name	*naml;		/* logical record for passed name */
	io_log_name	*tl;		/* logical record for translated name */
	mstr		tn;			/* translated name 	  */

	MV_FORCE_STR(v);
	MV_FORCE_STR(p);
	if (mspace)
		MV_FORCE_STR(mspace);
	assert((unsigned char)*p->str.addr < n_iops);
	naml = get_log_name(&v->str, INSERT);
	if (0 != naml->iod)
		tl = naml;
	else
	{
#		ifdef	NOLICENSE
		licensed= TRUE ;
#		else
		CRYPT_CHKSYSTEM;
		if (!licensed || LP_CONFIRM(lid,lkid)==LP_NOTACQ)
			licensed= FALSE ;
#		endif
		switch (stat = trans_log_name(&v->str, &tn, &buf1[0], SIZEOF(buf1), dont_sendmsg_on_log2long))
		{
		case SS_NORMAL:
			tl = get_log_name(&tn, INSERT);
			break;
		case SS_NOLOGNAM:
			tl = naml;
			break;
		case SS_LOG2LONG:
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_LOGTOOLONG, 3, v->str.len, v->str.addr, SIZEOF(buf1) - 1);
			break;
		default:
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) stat);
		}
	}
	stat = mu_open_try(naml, tl, p, mspace);
	return (stat);
}

static boolean_t mu_open_try(io_log_name *naml, io_log_name *tl, mval *pp, mval *mspace)
{
	boolean_t	ichset_specified, ochset_specified, filecreated = FALSE;
	char		*buf, namebuf[LOGNAME_LEN + 1];
	d_rm_struct	*d_rm;
	int		char_or_block_special, file_des, fstat_res, oflag, p_offset, save_errno, umask_creat, umask_orig;
	int4		recordsize, status;
	io_desc		*iod;
	mstr		chset_mstr;
	mstr		tn;		/* translated name */
	struct stat	outbuf;
	unsigned char	ch;

	char_or_block_special = FALSE;
	file_des = -2;
	oflag = 0;
	tn.len = tl->len;
	if (tn.len > LOGNAME_LEN)
		tn.len = LOGNAME_LEN;
	tn.addr = tl->dollar_io;
	memcpy(namebuf, tn.addr, tn.len);
	namebuf[tn.len] = '\0';
	buf = namebuf;
	if (0 == naml->iod)
	{
		if (0 == tl->iod)
		{
			iod = (io_desc *)malloc(SIZEOF(io_desc));
			memset((char*)iod, 0, SIZEOF(io_desc));
			iod->pair.in  = iod;
			iod->pair.out = iod;
			iod->trans_name = tl;
			iod->type = n_io_dev_types;
			p_offset = 0;
			while (iop_eol != *(pp->str.addr + p_offset))
			{
				ch = *(pp->str.addr + p_offset++);
				if (iop_sequential == ch)
					iod->type = rm;
				if (IOP_VAR_SIZE == io_params_size[ch])
					p_offset += *(pp->str.addr + p_offset) + 1;
				else
					p_offset += io_params_size[ch];
			}
			tl->iod = iod;
		} else
			iod = tl->iod;
		if ((n_io_dev_types == iod->type) && mspace && mspace->str.len)
			iod->type = us;
		if (n_io_dev_types == iod->type)
		{
			if (0 == memvcmp(tn.addr, tn.len, sys_input.addr, sys_input.len))
			{
				file_des = 0;
				FSTAT_FILE(file_des, &outbuf, fstat_res);
				if (-1 == fstat_res)
				{
					save_errno = errno;
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5,
						  RTS_ERROR_LITERAL("fstat()"),
						  CALLFROM, save_errno);
				}
			} else
			{
				if (0 == memvcmp(tn.addr, tn.len, sys_output.addr, sys_output.len))
				{
					file_des = 1;
					FSTAT_FILE(file_des, &outbuf, fstat_res);
					if (-1 == fstat_res)
					{
						save_errno = errno;
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5,
							  RTS_ERROR_LITERAL("fstat()"),
							  CALLFROM, save_errno);
					}
				} else if (0 == memvcmp(tn.addr, tn.len, "/dev/null", 9))
					iod->type = nl;
				else if ((-1 == Stat(buf, &outbuf)) && (n_io_dev_types == iod->type))
				{

					if (ENOENT == errno)
					{
						iod->type = rm;
						filecreated = TRUE;
					}
					else
					{
						save_errno = errno;
						rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5,
							  RTS_ERROR_LITERAL("fstat()"),
							  CALLFROM, save_errno);
					}
				}
			}
		}
		if (n_io_dev_types == iod->type)
		{
			switch (outbuf.st_mode & S_IFMT)
			{
				case S_IFCHR:
				case S_IFBLK:
					char_or_block_special = TRUE;
					break;
				case S_IFIFO:
					iod->type = ff;
					break;
				case S_IFREG:
				case S_IFDIR:
					iod->type = rm;
					break;
				case S_IFSOCK:
				case 0:
					iod->type = ff;
					break;
				default:
					break;
			}
		}
		naml->iod = iod;
	} else
		iod = naml->iod;
	active_device = iod;
	if ((-2 == file_des) && (dev_open != iod->state) && (us != iod->type))
	{
		oflag |= (O_RDWR | O_CREAT | O_NOCTTY);
		p_offset = 0;
		ichset_specified = ochset_specified = FALSE;
		while(iop_eol != *(pp->str.addr + p_offset))
		{
			assert((params) *(pp->str.addr + p_offset) < (params)n_iops);
			switch ((ch = *(pp->str.addr + p_offset++)))
			{
				case iop_append:
					if (rm == iod->type)
						oflag |= O_APPEND;
					break;
				case iop_contiguous:
					break;
				case iop_newversion:
					if ((dev_open != iod->state) && (rm == iod->type))
						oflag |= O_TRUNC;
					break;
				case iop_readonly:
					oflag  &=  ~(O_RDWR | O_CREAT | O_WRONLY);
					oflag  |=  O_RDONLY;
					break;
				case iop_writeonly:
					oflag  &= ~(O_RDWR | O_RDONLY);
					oflag  |= O_WRONLY | O_CREAT;
					break;
				case iop_ipchset:
#					ifdef KEEP_zOS_EBCDIC
					if ( (iconv_t)0 != iod->input_conv_cd )
					{
						ICONV_CLOSE_CD(iod->input_conv_cd);
					}
					SET_CODE_SET(iod->in_code_set,
						     (char *)(pp->str.addr + p_offset + 1));
					if (DEFAULT_CODE_SET != iod->in_code_set)
						ICONV_OPEN_CD(iod->input_conv_cd,
							      (char *)(pp->str.addr + p_offset + 1), INSIDE_CH_SET);
					break;
#					endif
					if (gtm_utf8_mode)
					{
						chset_mstr.addr = (char *)(pp->str.addr + p_offset + 1);
						chset_mstr.len = *(pp->str.addr + p_offset);
						SET_ENCODING(iod->ichset, &chset_mstr);
						ichset_specified = TRUE;
					}
					break;
				case iop_opchset:
#					ifdef KEEP_zOS_EBCDIC
					if ( (iconv_t)0 != iod->output_conv_cd)
					{
						ICONV_CLOSE_CD(iod->output_conv_cd);
					}
					SET_CODE_SET(iod->out_code_set,
						     (char *)(pp->str.addr + p_offset + 1));
					if (DEFAULT_CODE_SET != iod->out_code_set)
						ICONV_OPEN_CD(iod->output_conv_cd, INSIDE_CH_SET,
							      (char *)(pp->str.addr + p_offset + 1));
					break;
#					endif
					if (gtm_utf8_mode)
					{
						chset_mstr.addr = (char *)(pp->str.addr + p_offset + 1);
						chset_mstr.len = *(pp->str.addr + p_offset);
						SET_ENCODING(iod->ochset, &chset_mstr);
						ochset_specified = TRUE;
					}
					break;
				case iop_m:
				case iop_utf8:
				case iop_utf16:
				case iop_utf16be:
				case iop_utf16le:
					if (gtm_utf8_mode)
					{
						iod->ichset = iod->ochset =
							(iop_m       == ch) ? CHSET_M :
							(iop_utf8    == ch) ? CHSET_UTF8 :
							(iop_utf16   == ch) ? CHSET_UTF16 :
							(iop_utf16be == ch) ? CHSET_UTF16BE : CHSET_UTF16LE;
						ichset_specified = ochset_specified = TRUE;
					}
					break;
				default:
					break;
			}
			p_offset += ((IOP_VAR_SIZE == io_params_size[ch]) ?
				(unsigned char)*(pp->str.addr + p_offset) + 1 : io_params_size[ch]);
		}
		if (!ichset_specified)
			iod->ichset = (gtm_utf8_mode) ? CHSET_UTF8 : CHSET_M;
		if (!ochset_specified)
			iod->ochset = (gtm_utf8_mode) ? CHSET_UTF8 : CHSET_M;
		if (IS_UTF_CHSET(iod->ichset) && CHSET_UTF16 != iod->ichset)
			get_chset_desc(&chset_names[iod->ichset]);
		if (IS_UTF_CHSET(iod->ochset) && CHSET_UTF16 != iod->ochset)
			get_chset_desc(&chset_names[iod->ochset]);
		/* RW permissions for owner and others as determined by umask. */
		umask_orig = umask(000);	/* determine umask (destructive) */
		(void)umask(umask_orig);	/* reset umask */
		umask_creat = 0666 & ~umask_orig;
		/* the check for EINTR below is valid and should not be converte d to an EINTR
		 * wrapper macro, due to the other errno values being checked.
		 */
		while ((-1 == (file_des = OPEN3(buf, oflag, umask_creat))))
		{
			if (   EINTR == errno
			       || ETXTBSY == errno
			       || ENFILE == errno
			       || EBUSY == errno
			       || ((mb == iod->type) && (ENXIO == errno)))
				continue;
			else
				break;
		}
		if (-1 == file_des)
			return FALSE;
	}
#	ifdef KEEP_zOS_EBCDIC
	SET_CODE_SET(iod->in_code_set, OUTSIDE_CH_SET);
	if (DEFAULT_CODE_SET != iod->in_code_set)
		ICONV_OPEN_CD(iod->input_conv_cd, OUTSIDE_CH_SET, INSIDE_CH_SET);
	SET_CODE_SET(iod->out_code_set, OUTSIDE_CH_SET);
	if (DEFAULT_CODE_SET != iod->out_code_set)
		ICONV_OPEN_CD(iod->output_conv_cd, INSIDE_CH_SET, OUTSIDE_CH_SET);
#	endif
	assert(-1 != file_des); /* as otherwise we would have returned after the OPEN3 above */
	if (n_io_dev_types == iod->type)
	{
		if (isatty(file_des))
			iod->type = tt;
		else if (char_or_block_special && (2 < file_des))
		{	/* assume mag tape */
			iod->type = mt;
			assert(FALSE);
		} else
			iod->type = rm;
	}
	assert(iod->type < n_io_dev_types);
	iod->disp_ptr = &io_dev_dispatch_mupip[iod->type];
	active_device = iod;
	if (dev_never_opened == iod->state)
	{
		iod->wrap = DEFAULT_IOD_WRAP;
		iod->width = DEFAULT_IOD_WIDTH;
		iod->length = DEFAULT_IOD_LENGTH;
		iod->write_filter = 0; /* MUPIP should not use FILTER */
	}
	if (dev_open != iod->state)
	{
		iod->dollar.x = 0;
		iod->dollar.y = 0;
		iod->dollar.za = 0;
		iod->dollar.zb[0] = 0;
	}
	iod->newly_created = filecreated;
	status = (iod->disp_ptr->open)(naml, pp, file_des, mspace, NO_M_TIMEOUT);
	if (TRUE == status)
	{
		iod->state = dev_open;
		if (rm == iod->type)
		{
			d_rm = (d_rm_struct *)iod->dev_sp;
			if (!d_rm->def_recsize && d_rm->def_width)
			{
				iod->width = d_rm->recordsize;
				d_rm->def_width = FALSE;
			}
		}
	}
	else if (dev_open == iod->state)
		iod->state = dev_closed;
	if (1 == file_des)
		iod->dollar.zeof = TRUE;
	active_device = 0;
	iod->newly_created = FALSE;
	if (IS_MCODE_RUNNING)
		return (status);
	return TRUE;
}
