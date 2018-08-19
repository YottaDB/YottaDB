/****************************************************************
 *								*
 * Copyright (c) 2007-2015 Fidelity National Information 	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/*
 * The native object (ELF) wrapper has the following format:
 *
 *	+-------------------------------+
 *	| ELF header			|
 *	+-------------------------------+
 *	| .text section (GTM object)	|
 *	+-------------------------------+
 *	| .strtab section(string table)	|
 *	+-------------------------------+
 *	| .symtab section(sym table)	|
 *	+-------------------------------+
 *	| ELF Section header table	|
 *	+-------------------------------+
 *
 * The GT.M object layout (in the .text section) is described in obj_code.c
 *
 * This is a port of the ELF format to Mach-O by Sam Habiel.
 * Mach-O format:
 *
 *	+-------------------------------+
 *	| MACH-O 64bit Header (0 to 1C)	|
 *	+-------------------------------+
 *	| LC_SEGMENT64 (20 to 64)       |
 *	| -> SEC64 HEADER (__TEXT) (64 to B4) |
 *	+-------------------------------+
 *	| LC_SYMTAB (B8 to CC)          |
 *	+-------------------------------+
 *	| .text section (GTM object) (D0...)	|
 *	+-------------------------------+
 *	| .symtab section(sym table)	|
 *	+-------------------------------+
 *	| .strtab section(string table)	|
 *	+-------------------------------+
 
 *
 */

#include "mdef.h"

/* Mach-O stuff. See man 5 Mach-O */
#include <mach-o/loader.h> /* mach_header_64 */
#include <mach-o/nlist.h>  /* nlist_64 -- symbol table */
#include <mach-o/stab.h>   /* helper for that */

#include <errno.h>
#include "gtm_fcntl.h"
#include "gtm_unistd.h"
#include "gtm_stdio.h"
#include "gtm_string.h"

#include "compiler.h"
#include <rtnhdr.h>
#include "obj_gen.h"
#include "cgp.h"
#include "mdq.h"
#include "cmd_qlf.h"
#include "objlabel.h"	/* needed for masscomp.h */
#include "stringpool.h"
#include "parse_file.h"
#include "gtmio.h"
#include "mmemory.h"
#include "obj_file.h"
#include <obj_filesp.h>
#include "release_name.h"
#include "min_max.h"

/* The following definitions are required for the new(for ELF files) create/close_obj_file.c functions */
#ifdef __linux__
#define ELF64_LINKER_FLAG       0x10
#else
#define ELF64_LINKER_FLAG       0x18
#endif /* __linux__ */


/* Platform specific action instructions when routine called from foreign language. Returns -1 to caller */
#define MIN_LINK_PSECT_SIZE     0
LITDEF mach_inst jsb_action[JSB_ACTION_N_INS] = {0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff, 0xc3};


GBLREF command_qualifier cmd_qlf;
GBLREF char		object_file_name[];
GBLREF int		object_file_des;
GBLREF short		object_name_len;
GBLREF mident		module_name;
GBLREF boolean_t	run_time;
GBLREF int4		gtm_object_size;
DEBUG_ONLY(GBLREF int   obj_bytes_written;)

#define GTM_LANG        "MUMPS"
static char static_string_tbl[] = {
        /* Offset 0 */  '\0',
        /* Offset 1 */  '.', 't', 'e', 'x', 't', '\0',
        /* Offset 7 */  '.', 's', 't', 'r', 't', 'a', 'b', '\0',
        /* Offset 15 */ '.', 's', 'y', 'm', 't', 'a', 'b', '\0'
};

#define SPACE_STRING_ALLOC_LEN  (SIZEOF(static_string_tbl) +    \
                                 SIZEOF(GTM_LANG) + 1 +         \
                                 SIZEOF(GTM_PRODUCT) + 1 +      \
                                 SIZEOF(GTM_RELEASE_NAME) + 1 + \
                                 SIZEOF(mident_fixed))

/* Following constants has to be in sync with above static string array(static_string_tbl) */
#define STR_SEC_TEXT_OFFSET 1
#define STR_SEC_STRTAB_OFFSET 7
#define STR_SEC_SYMTAB_OFFSET 15

#define SEC_TEXT_INDX 1
#define SEC_STRTAB_INDX 2
#define SEC_SYMTAB_INDX 3

LITREF char gtm_release_name[];
LITREF int4 gtm_release_name_len;

GBLREF mliteral 	literal_chain;
GBLREF char 		source_file_name[];
GBLREF unsigned short 	source_name_len;
GBLREF mident		routine_name;
GBLREF mident		module_name;
GBLREF int4		mlmax, mvmax;
GBLREF int4		code_size, lit_addrs, lits_size;
GBLREF int4		psect_use_tab[];	/* bytes of each psect in this module */

/* Open the object file and write out the gtm object. Actual ELF creation happens at later stage during close_object_file */
void create_object_file(rhdtyp *rhead)
{
        assert(!run_time);
        DEBUG_ONLY(obj_bytes_written = 0);
 	init_object_file_name(); /* inputs: cmd_qlf.object_file, module_name; outputs: object_file_name, object_name_len */
	object_file_des = mk_tmp_object_file(object_file_name, object_name_len);
	/* Action instructions and marker are not kept in the same array since the type of the elements of
	 * the former (uint4) may be different from the type of the elements of the latter (char).
	 * 'tiz cleaner this way rather than converting one to the other type in order to be accommodated
	 * in an array.
	 */
        assert(JSB_ACTION_N_INS * SIZEOF(jsb_action[0]) == SIZEOF(jsb_action));	/* JSB_ACTION_N_INS maintained? */
        assert(SIZEOF(jsb_action) <= SIZEOF(rhead->jsb));			/* Overflow check */

  	memcpy(rhead->jsb, (char *)jsb_action, SIZEOF(jsb_action)); 		/* Action instructions */
        memcpy(&rhead->jsb[SIZEOF(jsb_action)], JSB_MARKER,			/* Followed by GTM_CODE marker */
               MIN(STR_LIT_LEN(JSB_MARKER), SIZEOF(rhead->jsb) - SIZEOF(jsb_action)));
        emit_immed((char *)rhead, SIZEOF(*rhead));
}

/* At this point, we know only gtm_object has been written onto the file.
 * Read that gtm_object and wrap it up in .text section, add remaining sections to native object(ELF)
 * Update the ELF, write it out to the object file and close the object file.
 */

/* WARNING: UNDER CONSTRUCTION NOW --> CONVERTING TO MACH-O LOADER */
void finish_object_file(void)
{
        int		i, status;
        size_t		bufSize;
        ssize_t		actualSize;
        char		*gtm_obj_code, *string_tbl;
        int		symIndex, strEntrySize;
        /*
        Elf		*elf;
        Elf64_Ehdr	*ehdr;
        Elf64_Shdr	*shdr, *text_shdr, *symtab_shdr, *strtab_shdr;
        Elf_Scn		*text_scn, *symtab_scn, *strtab_scn;
        Elf_Data	*text_data, *symtab_data, *strtab_data;
        Elf64_Sym	symEntries[3];
        */
        
        /* (sam): .strtab */
        /* (sam): Code not touched except change symIndex and add moduleOffset variables at the end */
        buff_flush();
        bufSize = gtm_object_size;
        actualSize = 0;
        string_tbl = malloc(SPACE_STRING_ALLOC_LEN);
        symIndex = 0;
        strEntrySize = SIZEOF(static_string_tbl);
        memcpy((string_tbl + symIndex), static_string_tbl, strEntrySize);
        symIndex += strEntrySize;
        strEntrySize = SIZEOF(GTM_LANG);
        memcpy((string_tbl + symIndex), GTM_LANG, strEntrySize);
        symIndex += strEntrySize;
        strEntrySize = SIZEOF(GTM_PRODUCT);
        memcpy((string_tbl + symIndex), GTM_PRODUCT, strEntrySize);
        symIndex += strEntrySize;
        strEntrySize = SIZEOF(GTM_RELEASE_NAME);
        memcpy((string_tbl + symIndex), GTM_RELEASE_NAME, strEntrySize);
        symIndex += strEntrySize;
        gtm_obj_code = (char *)malloc(bufSize);

        memcpy((string_tbl +  symIndex), module_name.addr, module_name.len);
        string_tbl[symIndex + module_name.len] = '\0';
        int moduleOffset = symIndex;
        symIndex += module_name.len + 1;

        /* At this point, we have only the GTM object written onto the file.
         * We need to read it back and wrap inside the ELF object and
         * write a native ELF object file.
         * (sam:) .text loaded into gtm_obj_code size bufSize
	     */
        lseek(object_file_des, 0, SEEK_SET);
        DOREADRL(object_file_des, gtm_obj_code, bufSize, actualSize);
        /* Reset the pointer back for writing an ELF object. */
        lseek(object_file_des, 0, SEEK_SET);

        /* Creating .symbtab section */
        int SYMTAB_SYMBOLS_NUMBER = 3;
        struct nlist_64 symtab[SYMTAB_SYMBOLS_NUMBER];
        i = 0;
        /* NULL symbol */
        symtab[i].n_un.n_strx = 0; /* Index into str tab */
        symtab[i].n_type = N_TYPE|N_UNDF; /* type flag ELF: ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE);*/
        symtab[i].n_sect = NO_SECT; /* Section Number */
        symtab[i].n_desc = N_LSYM; /* Local/Global */
        symtab[i].n_value = 0LL; /* value or stab offset */
        i++;
        /* Module symbol */
        symtab[i].n_un.n_strx = moduleOffset; /* Index into str tab */
        symtab[i].n_type = N_TYPE|N_SECT; /* ELF64_ST_INFO(STB_GLOBAL, STT_FUNC); No func in Mach-O */
        symtab[i].n_sect = 1; /* string table */
        symtab[i].n_desc = N_GSYM; /* Global */
        symtab[i].n_value = 0LL;
        i++;
        /* .text symbol */
        symtab[i].n_un.n_strx = STR_SEC_TEXT_OFFSET; /* Index into str tab */
        symtab[i].n_type = N_TYPE|N_SECT; /* ELF64_ST_INFO(STB_GLOBAL, STT_FUNC); No func in Mach-O */
        symtab[i].n_sect = 1; /* string table */
        symtab[i].n_desc = N_LSYM; /* Global */
        symtab[i].n_value = 0LL;

        /* Generate the top segement (LC_SEGMENT_64) */
        struct segment_command_64 topSeg;
        memset(&topSeg,0,sizeof(topSeg));

        topSeg.cmd = LC_SEGMENT_64;
        topSeg.nsects = 1;
        topSeg.cmdsize = sizeof(struct segment_command_64) +
                         sizeof(struct section_64) * topSeg.nsects;
        /* topSeg.segname -- don't set -- supposed to be empty according to Apple in header file */
        topSeg.vmaddr = 0;
        topSeg.vmsize = actualSize;
        topSeg.fileoff = sizeof(struct mach_header_64) +
                         sizeof(struct segment_command_64) +
                         sizeof(struct section_64) * topSeg.nsects +
                         sizeof(struct symtab_command) ; /* Where __text starts */
        topSeg.filesize = topSeg.vmsize;
        topSeg.maxprot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
        topSeg.initprot = topSeg.maxprot;
        topSeg.flags = 0;


        /* Section 64 Header (__TEXT) */
        struct section_64 secText;
        memset(&secText,0,sizeof(secText));
        memcpy(&secText.sectname, "__text", 6);
        memcpy(&secText.segname, "__TEXT", 6); /* segment this section goes in */
        secText.addr = 0LL;          /* memory address of this section */
        secText.size = actualSize; /* size in bytes of this section */
        secText.offset = sizeof(struct mach_header_64) 
            + sizeof(struct segment_command_64) 
            + topSeg.nsects * sizeof(struct section_64)
            + sizeof(struct symtab_command);
        secText.align = 4; /* 16 bit */
        secText.reloff = 0;
        secText.nreloc = 0;
        secText.flags = S_REGULAR | S_ATTR_SOME_INSTRUCTIONS;

        /* symtab command */
        struct symtab_command symtabCommand;
        memset(&symtabCommand,0,sizeof(symtabCommand));
        symtabCommand.cmd = LC_SYMTAB;
        symtabCommand.cmdsize = sizeof(struct symtab_command);
        symtabCommand.symoff = sizeof(struct mach_header_64) + 
            sizeof(struct segment_command_64) + 
            topSeg.nsects * sizeof(struct section_64) + 
            sizeof(struct symtab_command) +
            secText.size;
        symtabCommand.nsyms = SYMTAB_SYMBOLS_NUMBER; /* number of symbol table entries */
        symtabCommand.stroff = symtabCommand.symoff + sizeof(symtab);
        symtabCommand.strsize = symIndex; /* string table size in bytes */


        /* Generate Mach-O 64bit header */
        struct mach_header_64 machoHeader;
        memset(&machoHeader, 0, sizeof(machoHeader));

        machoHeader.magic = MH_MAGIC_64;       /* mach magic number identifier */
        machoHeader.cputype = CPU_TYPE_X86_64;  /* cpu specifier */
        machoHeader.cpusubtype = CPU_SUBTYPE_X86_ALL;   /* machine specifier */
        machoHeader.filetype = MH_OBJECT;   /* type of file */
        machoHeader.ncmds = 2;  /* number of load commands */
        machoHeader.sizeofcmds = sizeof(struct segment_command_64) + 
            sizeof(struct section_64) * topSeg.nsects + 
            sizeof(struct symtab_command); /* the size of all the load commands */
        machoHeader.flags = 0;      /* flags */
        machoHeader.reserved = 0;   /* reserved */

        /*****/
        /* (sam): Get rid of this later. Keep now; it may have good info.
        strtab_data->d_align = NATIVE_WSIZE;
        strtab_data->d_buf = string_tbl;
        strtab_data->d_off = 0LL;
        strtab_data->d_size = SPACE_STRING_ALLOC_LEN;
        symtab_data->d_align = NATIVE_WSIZE;
        symtab_data->d_off  = 0LL;
        symtab_data->d_buf  = symEntries;
        symtab_data->d_type = ELF_T_REL;
        symtab_data->d_size = SIZEOF(Elf64_Sym) * i;
        symtab_data->d_version = EV_CURRENT;
        if (NULL == (symtab_shdr = elf64_getshdr(symtab_scn)))
        {
                FPRINTF(stderr, "elf_getshdr() failed for symtab section!\n");
                assertpro(FALSE);
        }
        symtab_shdr->sh_name = STR_SEC_SYMTAB_OFFSET;
        symtab_shdr->sh_type = SHT_SYMTAB;
        symtab_shdr->sh_entsize = SIZEOF(Elf64_Sym) ;
        symtab_shdr->sh_link = SEC_STRTAB_INDX;
        elf_flagehdr(elf, ELF_C_SET, ELF_F_DIRTY);
        if (0 > elf_update(elf, ELF_C_WRITE))
        {
                FPRINTF(stderr, "elf_update() failed!\n");
                assertpro(FALSE);
        }
        elf_end(elf);
        */
        DOWRITE(object_file_des, &machoHeader, sizeof(machoHeader));
        DOWRITE(object_file_des, &topSeg, sizeof(topSeg));
        DOWRITE(object_file_des, &secText, sizeof(secText));
        DOWRITE(object_file_des, &symtabCommand, sizeof(symtabCommand));
        DOWRITE(object_file_des, gtm_obj_code, bufSize);
        DOWRITE(object_file_des, &symtab, sizeof(symtab));
        DOWRITE(object_file_des, string_tbl, symIndex);

        /* Free the memory malloc'ed above */
        free(string_tbl);
        free(gtm_obj_code);
}
