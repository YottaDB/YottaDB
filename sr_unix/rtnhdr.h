/****************************************************************
 *								*
 * Copyright (c) 2001-2015 Fidelity National Information 	*
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
#ifndef RTNHDR_H_INCLUDED
#define RTNHDR_H_INCLUDED

#include "srcline.h"

/* rtnhdr.h - routine header for shared binary Unix platforms */

/* There are several references to this structure from assembly language; these include:
 *
 * From Unix:	g_msf.si
 *
 * Any changes to the routine header must be reflected in those files as well.
 *
 * Warning: the list above may not be complete.
 */

/* Variable table entry */
typedef mname_entry var_tabent; /* the actual variable name is stored in the literal text pool */

/* Linenumber table entry */
typedef int4 lnr_tabent;

/* Label table entry */
typedef struct
{
	mident			lab_name;	/* The name of the label */
	lnr_tabent		*lnr_adr;	/* Pointer to lnrtab entry offset into code for this label */
	boolean_t		has_parms;	/* Flag to indicate whether the callee has a formallist */
	GTM64_ONLY(int4		filler;)
} lab_tabent;

/* Linkage table entry */
typedef struct
{
	char_ptr_t		ext_ref;	/* Address (quadword on alpha) this linkage entry resolves to or NULL */
} lnk_tabent;

#ifdef AUTORELINK_SUPPORTED
#include "relinkctl.h"		/* Needed for open_relinkctl_sgm type in zro_validation_entry */
/* Link search history entry - contains information to find the record in the relevant relinkctl file and what
 * the cycle was when loaded.
 */
typedef struct
{
	uint4			cycle;			/* Copy of relinkctl file cycle when loaded */
	relinkrec_t		*relinkrec;		/* Pointer to relinkctl record */
	open_relinkctl_sgm	*relinkctl_bkptr;	/* Address of mapped relinkctl file */
} zro_validation_entry;
/* Header for search history block */
typedef struct
{
	uint4			zroutines_cycle;	/* Value of set_zroutines_cycle when history created */
	zro_validation_entry	*end;			/* -> Last record + 1 */
	zro_validation_entry	base[1];		/* First history record (others follow) */
} zro_hist;
/* Structure used to queue up a list of validation entries and routine names before finalizing them into a
 * zro_hist structure.
 */
typedef struct
{
	zro_validation_entry	zro_valent;		/* Validation entry data (this record) */
	mident_fixed		rtnname;		/* Routine name associated with this validation entry */
	int4			rtnname_len;
} zro_search_hist_ent;
#endif

/* rhead_struct is the routine header; it occurs at the beginning of the
 * object code part of each module. Since this structure may be resident in
 * a shared library, this structure is considered inviolate. Therefore there is
 * a process-private version of this header that is modified as necessary and
 * always points to the current version.
 *
 * The routine header is initialized when a module is first linked into
 * an executable. The fields marked with "(#)" are updated when the routine
 * is replaced by a newer version via explicit zlink.
 */
typedef struct	rhead_struct
{
	char			jsb[RHEAD_JSB_SIZE];	/* GTM_CODE object marker */
	void_ptr_t		shlib_handle;		/* Null if header not for shared object. Non-zero means header is
							 * describing shared library resident routine and this is its handle.
							 * Note this is an 8 byte field on Tru64 (hence its position near top).
							 */
	mstr			src_full_name;		/* (#) Fully qualified path of routine source code */
	uint4			compiler_qlf;		/* Bit flags of compiler qualifiers used (see cmd_qlf.h) */
	uint4			objlabel;		/* Object code level/label (see objlable.h).
							 * Note: this field must be the 10th word (11th on Tru64) on 32-bit
							 * environments so that incr_link() can deference object label from old
							 * (pre-V5 32-bit) objects as well. In 64-bit environments though, this
							 * situation wouldn't occur since dlopen() would/should have failed
							 * when a 32-bit shared library is loaded
							 */
	mident			routine_name;		/* External routine name */
	var_tabent		*vartab_adr;		/* (#) Address of variable table (offset in original rtnhdr) */
	int4			vartab_len;		/* (#) Number of variable table entries */
	lab_tabent		*labtab_adr;		/* Address of label table (offset in original rtnhdr) */
	int4			labtab_len;		/* Number of label table entries */
	lnr_tabent		*lnrtab_adr;		/* Address of linenumber table (offset in original rtnhdr) */
	int4			lnrtab_len;		/* Number of linenumber table entries */
	unsigned char		*literal_text_adr;	/* Address of literal text pool (offset in original rtnhdr) */
	int4			literal_text_len;	/* Length of literal text pool */
	boolean_t		shared_object;		/* Linked as a shared object */
	mval			*literal_adr;		/* (#) Address of literal mvals (offset in original rtnhdr) */
	int4			literal_len;		/* Number of literal mvals */
	lnk_tabent		*linkage_adr;		/* (#) Address of linkage Psect (offset in original rtnhdr) */
	int4			linkage_len;		/* Number of linkage entries */
	int4			rel_table_off;		/* Offset to relocation table (not kept) */
	int4			sym_table_off;		/* Offset to symbol table (not kept) */
	boolean_t		rtn_relinked;		/* Routine has been relinked while this version was active. Check on
							 * unwind to see if this routine needs to be cleaned up. */
	unsigned char		*shared_ptext_adr;	/* If shared routine (shared library or object), points to shared copy */
	unsigned char		*ptext_adr;		/* (#) address of start of instructions (offset in original rtnhdr)
							 * If shared routine, points to shared copy unless breakpoints are active.
							 * In that case, points to private copy.
							 */
	unsigned char		*ptext_end_adr;		/* (#) Address of end of instructions + 1 (offset in original rtnhdr) */
	int4			checksum;		/* 4-byte source code checksum (for platforms where MD5 is unavailable) */
	int4			temp_mvals;		/* (#) temp_mvals value of current module version */
	int4			temp_size;		/* (#) temp_size value of current module version */
	boolean_t		has_ZBREAK;		/* This routine has a ZBREAK in it - disable it for autorelink */
	struct rhead_struct	*current_rhead_adr;	/* (#) Address of routine header of current module version */
	struct rhead_struct	*old_rhead_adr;		/* (#) Chain of replaced routine headers */
#	ifdef GTM_TRIGGER
	void_ptr_t		trigr_handle;		/* Type is void to avoid needing gv_trigger.h for gv_trigger_t type addr */
#	else
	void_ptr_t		filler1;
#	endif
	unsigned char		checksum_128[16];	/* 16-byte MurmurHash3 checksum of routine source code */
	struct rhead_struct	*active_rhead_adr;	/* addr of copy of this (original) header when that new version replaced
							 * this routine and was on M-Stack. See handle_active_versions() rtn. */
	routine_source		*source_code;		/* Source code used by $TEXT */
	ARLINK_ONLY(zro_hist	*zhist;)		/* If shared object -> validation list/array */
	gtm_uint64_t		objhash;		/* When object file is created, contains hash of object file */
	unsigned char		*lbltext_ptr;		/* Label name text blob if shared object replaced */
	uint4			object_len;		/* Length of wrapped GT.M object */
	uint4			routine_source_offset;	/* Offset of M source within literal text pool */
	mstr			*linkage_names;		/* Offset to mstr table of symbol names indexed same as linkage table */
#	ifdef AUTORELINK_SUPPORTED
	open_relinkctl_sgm	*relinkctl_bkptr;	/* Back pointer to relinkctl file that loaded this shared rtnobj */
#	endif
} rhdtyp;

/* Routine table entry */
typedef struct
{
	mident			rt_name;		/* The name of the routine (in the literal text pool) */
	rhdtyp			*rt_adr;		/* Pointer to its routine header */
} rtn_tabent;

/* Linkage table proxy for run-time linking of indirects. This struct has some overloaded meanings:
 *   1. The group of fields rtnhdr_adr/lnr_adr act as a proxy/surrogate linkage table holding those
 *      two values. They are defined in code with the indexes 0 and 1 respectively.
 *   2. The group of fields lnr_adr/has_parms pretend they are the proxy part of a label table
 *   	entry. Assembler code expects has_parms to be in the location following lnr_adr.
 */
typedef struct
{
	rhdtyp			*rtnhdr_adr;		/* Pointer to routine header for called routine */
	lnr_tabent		*lnr_adr;		/* Pointer to lnrtab entry offset into code for this label */
	boolean_t		has_parms;		/* Flag to indicate whether the callee has a formallist */
	int			filler1;		/* 64 bit alignment */
} lnk_tabent_proxy;
#define TABENT_PROXY TREF(lnk_proxy)

/* Byte offset of the routine_name field in the routine headers of pre-V5 releases */
#define PRE_V5_RTNHDR_RTNOFF		24

/* Byte offset of the routine_name mstr (len,addr) in V50 and V51 - only used in Tru64/HPUX-HPPA */
#if defined(__osf__) || defined(__hppa)
#  define V50V51_RTNHDR_RTNMSTR_OFFSET	24
#  ifdef __osf__
#    define V50V51_FTNHDR_LITBASE_OFFSET	68
#  elif defined(__hppa)
#    define V50V51_FTNHDR_LITBASE_OFFSET	64
#  else
#    error "Unsupported platform"
#  endif
typedef struct
{
	unsigned int	len;		/* Byte length */
	int4		*addr;		/* Offset at this stage */
} v50v51_mstr;
#endif

/* Macros for accessing routine header fields in a portable way */
#define VARTAB_ADR(rtnhdr) ((rtnhdr)->vartab_adr)
#define LABTAB_ADR(rtnhdr) ((rtnhdr)->labtab_adr)
#define LNRTAB_ADR(rtnhdr) ((rtnhdr)->lnrtab_adr)
#define LITERAL_ADR(rtnhdr) ((rtnhdr)->literal_adr)
#define LINKAGE_ADR(rtnhdr) ((rtnhdr)->linkage_adr)
#define PTEXT_ADR(rtnhdr) ((rtnhdr)->ptext_adr)
#define PTEXT_END_ADR(rtnhdr) ((rtnhdr)->ptext_end_adr)
#define CURRENT_RHEAD_ADR(rtnhdr) ((rtnhdr)->current_rhead_adr)
#define OLD_RHEAD_ADR(rtnhdr) ((rtnhdr)->old_rhead_adr)
#define LINE_NUMBER_ADDR(rtnhdr, lnr_tabent_adr) ((rtnhdr)->ptext_adr + *(lnr_tabent_adr))
#define LABENT_LNR_ENTRY(rtnhdr, lab_tabent_adr) ((lab_tabent_adr)->lnr_adr)
#define LABEL_ADDR(rtnhdr, lab_tabent_adr) (CODE_BASE_ADDR(rtnhdr) + *(LABENT_LNR_ENTRY(rtnhdr, lab_tabent_adr)))
#define CODE_BASE_ADDR(rtnhdr) ((rtnhdr)->ptext_adr)
#define CODE_OFFSET(rtnhdr, addr) ((char *)(addr) - (char *)(rtnhdr->ptext_adr))

#define DYNAMIC_LITERALS_ENABLED(rtnhdr) ((rtnhdr)->compiler_qlf & CQ_DYNAMIC_LITERALS)
#define RW_REL_START_ADR(rtnhdr) (((DYNAMIC_LITERALS_ENABLED(rtnhdr)) ? (char *)VARTAB_ADR(rtnhdr) : (char *)LITERAL_ADR(rtnhdr)))
#define PTEXT_OFFSET SIZEOF(rhdtyp)

/* Macro to determine if given address is inside code segment. Note that even though
 * the PTEXT_END_ADR macro is the address of end_of_code + 1, we still want a <= check
 * here because in many cases, the address being tested is the RETURN address from a
 * call that was done as the last instruction in the code segment. Sometimes this call
 * is to an error or it could be the implicit quit. On HPUX, the delay slot for the
 * implicit quit call at the end of the module can also cause the problem. Without
 * the "=" check also being there, the test will fail when it should succeed.
 */
#define ADDR_IN_CODE(caddr, rtnhdr) (PTEXT_ADR((rtnhdr)) <= (caddr) && (caddr) <= PTEXT_END_ADR((rtnhdr)))

/* Types that are different depending on shared/unshared unix binaries */
#define LABENT_LNR_OFFSET lnr_adr

/* When a routine is recursively linked, the old routine hdr/label table are copied and
 * attached to active_rhead_adr pointer of the replaced routine header. At unwind or goto time,
 * we check if these copies can be cleaned up with the following macro.
 */
#define CLEANUP_COPIED_RECURSIVE_RTN(RTNHDR)											\
{																\
	/* For USHBIN_SUPPORTED routines, if the rtn_relinked flag is ON in the routine header (which results from not cleaning	\
	 * up a routine when it is either explicitly or automatically re-linked), check if the stack contains any more		\
	 * references to this routine header. If not, it is ripe for cleanup as it has been replaced so drive zr_unlink_rtn()	\
	 * on it. The RTNHDR being checked for non-NULL is due to a call-in base frame not having a rtnhdr value.		\
	 */															\
	if ((NULL != (RTNHDR)) && (RTNHDR)->rtn_relinked)									\
		zr_cleanup_recursive_rtn(RTNHDR);										\
}
/* Format of a relocation datum. */
struct	relocation_info
{
		 int	r_address;	/* address which is relocated */
	unsigned int	r_symbolnum;	/* local symbol ordinal */
};

struct	rel_table
{
	struct rel_table	*next,
				*resolve;
	struct relocation_info	r;
};

/* Format of a symbol table entry; this file is included by <a.out.h>
 * and should be used if you aren't interested the a.out header
 * or relocation information.
 */
#ifdef __APPLE__
#include <mach-o/nlist.h>
#else
struct	nlist
{
	int4		n_type;		/* Type flag, i.e. N_TEXT etc; see below */
	uint4		n_value;	/* Value of this symbol (or sdb offset) */
};
#endif

struct	sym_table
{
	struct sym_table	*next;
	struct nlist		n;
	struct rel_table	*resolve;
	int4			linkage_offset;
	unsigned short		name_len;
	unsigned char		name[1];
};

/* Simple values for n_type. */
#define	N_TEXT	0x04		/* Text */
#define	N_EXT	0x01		/* cexternal bit, or'ed in */

/* Flag values for get_src_line call */
#define VERIFY		TRUE
#define NOVERIFY	FALSE

/* Prototypes */
int get_src_line(mval *routine, mval *label, int offset, mstr **srcret, rhdtyp **rtn_vec);
void free_src_tbl(rhdtyp *rtn_vector);
unsigned char *find_line_start(unsigned char *in_addr, rhdtyp *routine);
int4 *find_line_addr(rhdtyp *routine, mstr *label, int4 offset, mident **lent_name);
rhdtyp *find_rtn_hdr(mstr *name);
boolean_t find_rtn_tabent(rtn_tabent **res, mstr *name);
bool zlput_rname(rhdtyp *hdr);
void zlmov_lnames(rhdtyp *hdr);
rhdtyp *make_dmode(void);
void comp_lits(rhdtyp *rhead);
int op_rhdaddr(mval *name, int rtnidx);
int op_labaddr(int rtnidx, mval *label, int4 offset);
void urx_resolve(rhdtyp *rtn, lab_tabent *lbl_tab, lab_tabent *lbl_top);
char *rtnlaboff2entryref(char *entryref_buff, mident *rtn, mident *lab, int offset);
boolean_t on_stack(rhdtyp *rtnhdr, boolean_t *need_duplicate);
rhdtyp *op_rhd_ext(mval *rtname, mval *lblname, rhdtyp *rhd, void *lnr);
void *op_lab_ext(void);
void zr_cleanup_recursive_rtn(rhdtyp *rtnhdr);
#ifdef AUTORELINK_SUPPORTED
#include "zroutinessp.h"	/* Needed for zro_ent type for zro_record_zhist declaration */
boolean_t need_relink(rhdtyp *rtnhdr, zro_hist *zhist);
zro_hist *zro_zhist_saverecent(zro_search_hist_ent *zhist_valent, zro_search_hist_ent *zhist_valent_base);
void zro_record_zhist(zro_search_hist_ent *zhist_valent, zro_ent *obj_container, mstr *rtnname);
#endif /* AUTORELINK_DEFINED */

#endif /* RTNHDR_H_INCLUDED */
