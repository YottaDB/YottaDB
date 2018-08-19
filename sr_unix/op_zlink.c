/****************************************************************
 *								*
 * Copyright (c) 2001-2017 Fidelity National Information	*
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
#include "gtm_stat.h"
#include "gtm_string.h"
#include "gtm_unistd.h"
#include <errno.h>

#include "stringpool.h"
#ifdef AUTORELINK_SUPPORTED
# include "rtnhdr.h"
#endif
#include "zroutines.h"
#include "cmd_qlf.h"
#include "parse_file.h"
#include "gtmio.h"
#include "eintr_wrappers.h"
#include "op.h"
#include "incr_link.h"
#include "compiler.h"
#ifdef __MVS__
# include "gtm_zos_io.h"
#endif
#include "arlinkdbg.h"

typedef enum
{
	SRC = 1,
	OBJ,
	NOTYPE
} linktyp;

/* On shared platforms, skip parameter should be FALSE to indicate an auto-ZLINK so that
 * zro_search looks into shared libraries. On non-shared platforms, it should be
 * TRUE to instruct zro_search to always skip shared libraries
 */
#define SKIP_SHLIBS	TRUE
#ifdef USHBIN_SUPPORTED
#define PROBE_SHLIBS	(!SKIP_SHLIBS) /* i.e., don't skip (skip = FALSE) */
#else
#define PROBE_SHLIBS	SKIP_SHLIBS
#endif

/* On certain platforms the st_mtime field of the stat structure got replaced by a timespec st_mtim field, which in turn has tv_sec
 * and tv_nsec fields. For compatibility reasons, those platforms define an st_mtime macro which points to st_mtim.tv_sec. Whenever
 * we detect such a situation, we define a nanosecond flavor of that macro to point to st_mtim.tv_nsec. On HPUX Itanium and older
 * AIX boxes the stat structure simply has additional fields with the nanoseconds value, yet the names of those field are different
 * on those two architectures, so we choose our mapping accordingly.
 */
#if defined st_mtime
# ifdef __APPLE__
#   define st_nmtime		st_mtimespec.tv_nsec
# else
#   define st_nmtime		st_mtim.tv_nsec
# endif 
#elif defined(_AIX)
#  define st_nmtime		st_mtime_n
#elif defined(__hpux) && defined(__ia64)
#  define st_nmtime		st_nmtime
#endif
/* Macro to close object file and give appropriate error */
#define CLOSE_OBJECT_FD(FD, STATUS)												\
{																\
	CLOSE_OBJECT_FILE(FD, STATUS);	/* Resets "object_file_des" to FD_INVALID */						\
	if (-1 == (STATUS))													\
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_SYSCALL, 5, RTS_ERROR_LITERAL("close()"), CALLFROM, errno);	\
}

#ifdef AUTORELINK_SUPPORTED
#  define CHECK_OBJECT_HISTORY(OBJPATH, OBJDIR, RECENT_ZHIST_PARM)								\
{																\
	if (TREF(arlink_enabled) && !TREF(trigger_compile_and_link))								\
	{	/* Autorelink is enabled, this is not a trigger and we need a search history for the object file to pass	\
		 * to incr_link(). We had to wait till this point to discover the search history since at the time of the	\
		 * call to zro_search(), there may not have been an object file to find or, given where we found the source,	\
		 * the object file we are linking now may be different from one found before so create the history array given  \
		 * the supplied object file path and our $ZROUTINES directory array. See GTM-8311 for potential improvements	\
		 * in this area.												\
		 *														\
		 * Note we cannot assert RECENT_ZHIST_PARM is null here as we may be about to link a recompiled module after	\
		 * a failed ZLINK but incr_link() took care of releasing the old history in its error path.			\
		 */														\
		RECENT_ZHIST_PARM = zro_search_hist(OBJPATH, &OBJDIR);								\
	}															\
}
#else
#  define CHECK_OBJECT_HISTORY(OBJPATH, OBJDIR, RECENT_ZHIST_PARM)
#endif

GBLREF spdesc			rts_stringpool, stringpool;
GBLREF command_qualifier	glb_cmd_qlf, cmd_qlf;
GBLREF mval			dollar_zsource;
GBLREF int			object_file_des;

error_def(ERR_FILENOTFND);
error_def(ERR_FILEPARSE);
error_def(ERR_SYSCALL);
error_def(ERR_TEXT);
error_def(ERR_VERSION);
error_def(ERR_WILDCARD);
error_def(ERR_ZLINKFILE);
error_def(ERR_ZLNOOBJECT);
ZOS_ONLY(error_def(ERR_BADTAG);)

/* Routine to locate object files, or source and compile to an object file if necessary, and drive the linker to link the file
 * into this process appropriately. Three types of linking are currently supported on UNIX platforms (excepting Linux i386 which
 * has its own less fully-featured linker):
 *
 * 1. Link into process private - Executable code becomes part of the process private space.
 * 2. Link from a shared library - M routines linked into a shared library can be linked into a process allowing much of the
 *    object file to be shared.
 * 3. Link from a shared object - Shared objects are loaded into shared memory by GT.M (rtnobj.c is the manager) and linked
 *    much like objects linked from a shared library.
 *
 * Parameters:
 *   - v     - mval containing the name/path of the object file.
 *   - quals - mval containing the ZLINK command options (see GT.M User's Guide).
 */
void op_zlink (mval *v, mval *quals)
{
	int			status, qlf, tslash, save_errno;
	linktyp			type;
	char			srcnamebuf[MAX_FBUFF + 1], objnamebuf[MAX_FBUFF + 1], *fname;
	uint4			objnamelen, srcnamelen;
	char			inputf[MAX_FBUFF + 1], obj_file[MAX_FBUFF + 1], list_file[MAX_FBUFF + 1],
				ceprep_file[MAX_FBUFF + 1];
	zro_ent			*srcdir, *objdir;
	mstr			srcstr, objstr, file;
	boolean_t		compile, expdir, obj_found, src_found;
	parse_blk		pblk;
	mval			qualifier;
	struct stat		obj_stat, src_stat;
	ARLINK_ONLY(zro_hist	*recent_zhist;)
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	ARLINK_ONLY(recent_zhist = NULL);
	MV_FORCE_STR(v);
	if (!v->str.len)
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_ZLINKFILE, 2, v->str.len, v->str.addr, ERR_TEXT, 2,
			      RTS_ERROR_LITERAL("Filename/path is missing"));
	if (MAX_FBUFF < v->str.len)
		rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_ZLINKFILE, 2, v->str.len, v->str.addr, ERR_TEXT, 2,
			      RTS_ERROR_LITERAL("Filename/path exceeds max length"));
	DBGARLNK((stderr, "op_zlink: Call to (re)link routine %.*s\n", v->str.len, v->str.addr));
	object_file_des = FD_INVALID;
	srcdir = objdir = NULL;
	expdir = FALSE;
	if (quals)
	{	/* Explicit ZLINK from generated code or from gtm_trigger() */
		memset(&pblk, 0, SIZEOF(pblk));
		pblk.buff_size = MAX_FBUFF;
		pblk.buffer = inputf;
		pblk.fop = F_SYNTAXO;
		status = parse_file(&v->str, &pblk);
		if (!(status & 1))
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_FILEPARSE, 2, v->str.len, v->str.addr, status);
		if (pblk.fnb & F_WILD)
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_ZLINKFILE, 2, v->str.len, v->str.addr,
				ERR_WILDCARD, 2, v->str.len, v->str.addr);
		file.addr = pblk.buffer;
		file.len = pblk.b_esl;
		type = NOTYPE;
		expdir = (0 != (pblk.fnb & F_HAS_DIR));
		if (pblk.b_ext)
		{
			file.len -= pblk.b_ext;
			if (('o' == pblk.l_ext[1]) && (2 == pblk.b_ext))
				type = OBJ;
			else
				type = SRC;
		}
		if (!expdir)
		{
			file.addr = pblk.l_name;
			file.len = pblk.b_name;
		}
		ENSURE_STP_FREE_SPACE(file.len);
		memcpy(stringpool.free, file.addr, file.len);
		dollar_zsource.str.addr = (char *) stringpool.free;
		dollar_zsource.str.len = file.len;
		stringpool.free += file.len;
		if (OBJ == type)
		{	/* Object file extension specified */
			memmove(objnamebuf, file.addr, file.len + pblk.b_ext);
			objnamelen = file.len + pblk.b_ext;
			assert (objnamelen <= MAX_FBUFF + 1);
			objnamebuf[objnamelen] = 0;
		} else if (SRC == type)
		{	/* Non-object file extension specified */
			memmove(srcnamebuf, file.addr,file.len + pblk.b_ext);
			srcnamelen = file.len + pblk.b_ext;
			assert (srcnamelen <= MAX_FBUFF + 1);
			srcnamebuf[srcnamelen] = 0;
			objnamelen = file.len;
			if (pblk.b_name > MAX_MIDENT_LEN)
				objnamelen = expdir ? (pblk.b_dir + MAX_MIDENT_LEN) : MAX_MIDENT_LEN;
			memcpy(objnamebuf, file.addr, objnamelen);
			memcpy(&objnamebuf[objnamelen], DOTOBJ, SIZEOF(DOTOBJ));	/* Copies null terminator */
			objnamelen += STR_LIT_LEN(DOTOBJ);
			assert (objnamelen + SIZEOF(DOTOBJ) <= MAX_FBUFF + 1);
		} else
		{	/* No file extension specified */
			if ((file.len + SIZEOF(DOTM) > SIZEOF(srcnamebuf)) ||
			    (file.len + SIZEOF(DOTOBJ) > SIZEOF(objnamebuf)))
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_ZLINKFILE, 2, v->str.len, v->str.addr);
			memmove(srcnamebuf, file.addr, file.len);
			memcpy(&srcnamebuf[file.len], DOTM, SIZEOF(DOTM));
			srcnamelen = file.len + SIZEOF(DOTM) - 1;
			assert (srcnamelen + SIZEOF(DOTM) <= MAX_FBUFF + 1);
			memcpy(objnamebuf,file.addr,file.len);
			memcpy(&objnamebuf[file.len], DOTOBJ, SIZEOF(DOTOBJ));		/* Copies null terminator */
			objnamelen = file.len + SIZEOF(DOTOBJ) - 1;
			assert (objnamelen + SIZEOF(DOTOBJ) <= MAX_FBUFF + 1);
		}
		if (!expdir)
		{	/* No full or relative directory specified on file to link - fill it in */
			objstr.addr = objnamebuf;
			objstr.len = objnamelen;
			if (OBJ == type)
			{	/* Explicit ZLINK of object - don't locate source */
				zro_search(&objstr, &objdir, NULL, NULL, SKIP_SHLIBS);
				if (NULL == objdir)
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_ZLINKFILE, 2, dollar_zsource.str.len,
						      dollar_zsource.str.addr, ERR_FILENOTFND,2, dollar_zsource.str.len,
						      dollar_zsource.str.addr);
			} else if (SRC == type)
			{	/* Explicit ZLINK of source - locate both source and object*/
				srcstr.addr = srcnamebuf;
				srcstr.len = srcnamelen;
				zro_search(&objstr, &objdir, &srcstr, &srcdir, SKIP_SHLIBS);
				if (NULL == srcdir)
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_ZLINKFILE, 2, srcnamelen, srcnamebuf,
						      ERR_FILENOTFND, 2, srcnamelen, srcnamebuf);
			} else
			{	/* Explicit ZLINK no file type specified - locate both source and object */
				srcstr.addr = srcnamebuf;
				srcstr.len = srcnamelen;
				zro_search(&objstr, &objdir, &srcstr, &srcdir, PROBE_SHLIBS);
				if ((NULL == objdir) && (NULL == srcdir))
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_ZLINKFILE, 2, dollar_zsource.str.len,
						      dollar_zsource.str.addr, ERR_FILENOTFND,2, dollar_zsource.str.len,
						      dollar_zsource.str.addr);
			}
		}
	} else
	{	/* auto-ZLINK for execution, ZBREAK, $TEXT(), or ZEDIT (i.e. non-specific call to op_zlink()) */
		type = NOTYPE;
		memcpy(srcnamebuf, v->str.addr, v->str.len);
		memcpy(&srcnamebuf[v->str.len], DOTM, SIZEOF(DOTM));
		srcnamelen = v->str.len + SIZEOF(DOTM) - 1;
		if ('%' == srcnamebuf[0])
			srcnamebuf[0] = '_';
		memcpy(objnamebuf, srcnamebuf, v->str.len);
		memcpy(&objnamebuf[v->str.len], DOTOBJ, SIZEOF(DOTOBJ));		/* Copies null terminator */
		objnamelen = v->str.len + SIZEOF(DOTOBJ) - 1;
		srcstr.addr = srcnamebuf;
		srcstr.len = srcnamelen;
		objstr.addr = objnamebuf;
		objstr.len = objnamelen;
		zro_search(&objstr, &objdir, &srcstr, &srcdir, PROBE_SHLIBS);
		if ((NULL == objdir) && (NULL == srcdir))
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_ZLINKFILE, 2, v->str.len, v->str.addr,
				      ERR_FILENOTFND, 2, v->str.len, v->str.addr);
		qualifier.mvtype = MV_STR;
		qualifier.str = TREF(dollar_zcompile);
		quals = &qualifier;
	}
	if (OBJ == type)
	{	/* Object file extension specified */
		if (objdir)
		{	/* Object file found via zro_search() */
			assert(ZRO_TYPE_OBJLIB != objdir->type);
			if (objdir->str.len + objnamelen + 2 > SIZEOF(objnamebuf))
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_ZLINKFILE, 2, v->str.len, v->str.addr);
			if (objdir->str.len)
			{
				tslash = ('/' == objdir->str.addr[objdir->str.len - 1]) ? 0 : 1;
				memmove(&objnamebuf[objdir->str.len + tslash], objnamebuf, objnamelen);
				if (tslash)
					objnamebuf[objdir->str.len] = '/';
				memcpy(objnamebuf, objdir->str.addr, objdir->str.len);
				objnamelen += objdir->str.len + tslash;
				objnamebuf[objnamelen] = 0;
			}
		}
		OPEN_OBJECT_FILE(objnamebuf, O_RDONLY, object_file_des);
		if (FD_INVALID == object_file_des)
			/* Could not find object file */
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_ZLINKFILE, 2, dollar_zsource.str.len, dollar_zsource.str.addr,
				      errno);
		/* Note - if explicit ZLINK, objdir can be NULL if link is from a directory not mentioned in $ZROUTINES */
		CHECK_OBJECT_HISTORY(objnamebuf, objdir, RECENT_ZHIST);
		if (IL_RECOMPILE == INCR_LINK(&object_file_des, objdir, RECENT_ZHIST, objnamelen, objnamebuf))
		{
			rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_ZLINKFILE, 2, dollar_zsource.str.len, dollar_zsource.str.addr,
				      ERR_VERSION);
		}
		CLOSE_OBJECT_FD(object_file_des, status);
	} else
	{	/* Either NO file extension specified or is SOURCE file extension type */
		cmd_qlf.object_file.str.addr = obj_file;
		cmd_qlf.object_file.str.len = MAX_FBUFF;
		cmd_qlf.list_file.str.addr = list_file;
		cmd_qlf.list_file.str.len = MAX_FBUFF;
		cmd_qlf.ceprep_file.str.addr = ceprep_file;
		cmd_qlf.ceprep_file.str.len = MAX_FBUFF;
		if (srcdir)
		{	/* A source directory containing routine was found by zro_search() */
			assert(ZRO_TYPE_OBJLIB != objdir->type);
			if (srcdir->str.len + srcnamelen > SIZEOF(srcnamebuf) - 1)
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_ZLINKFILE, 2, v->str.len, v->str.addr);
			if (srcdir->str.len)
			{
				tslash = ('/' == srcdir->str.addr[srcdir->str.len - 1]) ? 0 : 1;
				memmove(&srcnamebuf[srcdir->str.len + tslash], srcnamebuf, srcnamelen);
				if (tslash)
					srcnamebuf[srcdir->str.len] = '/';
				memcpy(srcnamebuf, srcdir->str.addr, srcdir->str.len);
				srcnamelen += srcdir->str.len + tslash;
				srcnamebuf[srcnamelen] = 0;
			}
		}
		if (objdir)
		{	/* An object directory or shared library containing the routine was found by zro_search() */
			if (ZRO_TYPE_OBJLIB == objdir->type)
			{	/* For object libraries, there are no auto-relink complications so no need to hunt down
				 * any history for it or to pass in any.
				 */
				assert(objdir->shrlib);
				assert(objdir->shrsym);
				/* The incr_link() routine should drive errors for any issue found with linking from a shared
				 * library so IL_DONE is the only valid return code we *ever* expect back.
				 */
				assertpro(IL_DONE == INCR_LINK(NULL, objdir, NULL, objnamelen, objnamebuf));
				return;
			}
			if (objdir->str.len + objnamelen > SIZEOF(objnamebuf) - 1)
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_ZLINKFILE, 2, v->str.len, v->str.addr);
			if (objdir->str.len)
			{
				tslash = ('/' == objdir->str.addr[objdir->str.len - 1]) ? 0 : 1;
				memmove(&objnamebuf[objdir->str.len + tslash], objnamebuf, objnamelen);
				if (tslash)
					objnamebuf[objdir->str.len] = '/';
				memcpy(objnamebuf, objdir->str.addr, objdir->str.len);
				objnamelen += objdir->str.len + tslash;
				objnamebuf[objnamelen] = 0;
			}
		}
		/* Check which source/object we ended up with and whether they are for the same file or not so we can determine if
		 * recompilation might be required.
		 */
		src_found = obj_found = compile = FALSE;
		if (SRC != type)
		{	/* Object or NO file extension specified - check object file exists */
			OPEN_OBJECT_FILE(objnamebuf, O_RDONLY, object_file_des);
			if (FD_INVALID == object_file_des)
			{
				if (ENOENT == errno)
					obj_found = FALSE;
				else
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_ZLINKFILE, 2, objnamelen, objnamebuf, errno);
			} else
				obj_found = TRUE;
		} else	/* If source file extension specified, force re-compile */
		{
			compile = TRUE;
			assert(FD_INVALID == object_file_des);	/* Shouldn't be an object file open yet */
		}
		STAT_FILE(srcnamebuf, &src_stat, status);	/* Check if source file exists */
		if (-1 == status)
		{
			if ((ENOENT == errno) && (SRC != type))
				src_found = FALSE;
			else
			{
				save_errno = errno;
				if (FD_INVALID != object_file_des)	/* Chose object file if open, ignore error */
					CLOSE_OBJECT_FILE(object_file_des, status);
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_ZLINKFILE, 2, srcnamelen, srcnamebuf, save_errno);
			}
		} else
			src_found = TRUE;
		if (SRC != type)
		{	/* If object or no file extension type specified, check if have both source and object and if so, decide
			 * if source needs to be recompiled.
			 */
			if (src_found)
			{
				if (obj_found)
				{
					FSTAT_FILE(object_file_des, &obj_stat, status);
					if (-1 == status)
					{
						save_errno = errno;
						if (FD_INVALID != object_file_des)	/* Close object file if open */
							CLOSE_OBJECT_FILE(object_file_des, status);
						rts_error_csa(CSA_ARG(NULL)VARLSTCNT(5) ERR_ZLINKFILE, 2, objnamelen, objnamebuf,
							      save_errno);
					}
					if ((src_stat.st_mtime > obj_stat.st_mtime)
					    || ((src_stat.st_mtime == obj_stat.st_mtime)
						&& (src_stat.st_nmtime > obj_stat.st_nmtime)))
					{
						CLOSE_OBJECT_FD(object_file_des, status);
						compile = TRUE;
					}
				} else
				{
					compile = TRUE;
					assert(FD_INVALID == object_file_des);	/* Make sure no object file open */
				}
			} else if (!obj_found)
			{
				assert(FD_INVALID == object_file_des);	/* Make sure closed */
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_ZLINKFILE, 2, objnamelen, objnamebuf,
					      ERR_FILENOTFND, 2, objnamelen, objnamebuf);
			}
		}
		if (compile)
		{	/* (Re)Compile source file */
			zl_cmd_qlf(&quals->str, &cmd_qlf);
			if (!MV_DEFINED(&cmd_qlf.object_file))
			{	/* -object= parameter NOT specified - fill in default */
				cmd_qlf.object_file.mvtype = MV_STR;
				cmd_qlf.object_file.str.addr = objnamebuf;
				cmd_qlf.object_file.str.len = objnamelen;
			}
			qlf = cmd_qlf.qlf;
			if (!(qlf & CQ_OBJECT) && (SRC != type))
			{
				cmd_qlf.qlf = glb_cmd_qlf.qlf;
				assert(FD_INVALID == object_file_des);	/* Make sure no object file open */
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_ZLINKFILE, 2, srcnamelen, srcnamebuf, ERR_ZLNOOBJECT);
			}
			zlcompile(srcnamelen, (uchar_ptr_t)srcnamebuf);
			assert(FD_INVALID == object_file_des);	/* zlcompile() should have driven obj_code() which closes object */
			assertpro(FALSE == TREF(compile_time) && (stringpool.base == rts_stringpool.base));	/* sb back in rts */
			if ((SRC == type) && !(qlf & CQ_OBJECT))
				return;
			OPEN_OBJECT_FILE(objnamebuf, O_RDONLY, object_file_des);
		}
		assert(FD_INVALID != object_file_des);		/* Object file should be open at this point */
		CHECK_OBJECT_HISTORY(objnamebuf, objdir, RECENT_ZHIST);
		status = INCR_LINK(&object_file_des, objdir, RECENT_ZHIST, objnamelen, objnamebuf);
		if (IL_RECOMPILE == status)
		{	/* Failure due only to version mismatch, so recompile */
			assertpro(!compile);
			if (!src_found)
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_ZLINKFILE, 2, srcnamelen, srcnamebuf, ERR_VERSION);
			zl_cmd_qlf(&quals->str, &cmd_qlf);
			if (!MV_DEFINED(&cmd_qlf.object_file))
			{
				cmd_qlf.object_file.mvtype = MV_STR;
				cmd_qlf.object_file.str.addr = objnamebuf;
				cmd_qlf.object_file.str.len = objnamelen;
			}
			zlcompile(srcnamelen, (uchar_ptr_t)srcnamebuf);
			assertpro(FALSE == TREF(compile_time) && (stringpool.base == rts_stringpool.base));	/* sb back in rts */
			if (!(cmd_qlf.qlf & CQ_OBJECT) && (SRC != type))
			{
				cmd_qlf.qlf = glb_cmd_qlf.qlf;
				rts_error_csa(CSA_ARG(NULL) VARLSTCNT(5) ERR_ZLINKFILE, 2, srcnamelen, srcnamebuf, ERR_ZLNOOBJECT);
			}
			assert(FD_INVALID == object_file_des);	/* zlcompile() should have driven obj_code() which closes it */
			OPEN_OBJECT_FILE(objnamebuf, O_RDONLY, object_file_des);
			CHECK_OBJECT_HISTORY(objnamebuf, objdir, RECENT_ZHIST);
			/* We just did a fresh re-compile a few lines above so IL_DONE is the only return code we ever
			 * expect to see back. Only a race-condition created by a different version overlaying the newly
			 * created object file could conceivably cause an IL_RECOMPILE code here (incr_link handles all
			 * the other errors itself). Not at this time considered worthy of special coding.
			 */
			assertpro(IL_DONE == INCR_LINK(&object_file_des, objdir, RECENT_ZHIST, objnamelen, objnamebuf));
		}
		CLOSE_OBJECT_FD(object_file_des, status);
	}
}
