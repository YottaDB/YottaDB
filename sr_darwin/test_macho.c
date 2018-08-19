/* Mach-O stuff. See man 5 Mach-O */
#include <mach-o/loader.h> /* mach_header_64 */
#include <mach-o/nlist.h>  /* nlist_64 -- symbol table */
#include <mach-o/stab.h>   /* helper for that */

#include <stdlib.h>        /* malloc */
#include <stdio.h>         /* fopen fclose perror etc */
#include <string.h>        /* strerror */

int main()
{
    /* NB: Mach-O objects need to be built in reverse order because we need
     * the sizes of all the stuff at the bottom to put the offsets at the
     * top. */

    /* This is the structure we want :
     * MACH64 HEADER (0 to 1C)
     * LC_SEGMENT64 (20 to 64)
     * -> SEC64 HEADER (__TEXT) (68 to B4) (Done)
     * LC_SYMTAB (B8 to CC) (Done)
     * __TEXT section (D0 -> ...) (variable length) (done)
     * Symbol Table section (after __TEXT) (Literal!)
     * String Table (Literal!)

    * What's below is in reverse order of the above list */

    /* String Table */
    char __strtab[] = "\0XUS\0";

    /* Sample Symbol table
     * from readelf:
     *    Num:    Value          Size Type    Bind   Vis      Ndx Name
     *    0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND 
     *    1: 0000000000000000 30048 FUNC    GLOBAL DEFAULT    1 XUS
    
TODO: There is no such thing as type "FUNC" that I can see.
    */
    struct nlist_64 symtab[2];
    memset(&symtab, 0, sizeof(symtab));

    symtab[0].n_un.n_strx = 0; /* Index into str tab */
    symtab[0].n_type = N_TYPE|N_UNDF; /* type flag */
    symtab[0].n_sect = NO_SECT; /* Section Number */
    symtab[0].n_desc = N_LSYM; /* Local/Global */
    symtab[0].n_value = 0LL; /* value or stab offset */

    symtab[1].n_un.n_strx = 1;
    symtab[1].n_type = N_TYPE|N_SECT;
    symtab[1].n_sect = 1; /* string table */
    symtab[1].n_desc = N_GSYM; /* Global */
    symtab[1].n_value = 0LL;

    /* Sample __Text Data */
    unsigned char __text[] = 
    {
        0x55, 0x48, 0x89, 0xE5, 0x48, 0x83
    };

    /* LC_SYMTAB */
    struct symtab_command symtabCommand;
    memset(&symtabCommand,0,sizeof(symtabCommand));
    symtabCommand.cmd = LC_SYMTAB;
    symtabCommand.cmdsize = sizeof(struct symtab_command);
    symtabCommand.symoff = sizeof(struct mach_header_64) + 
        sizeof(struct segment_command_64) + 
        1 * sizeof(struct section_64) + 
        sizeof(struct symtab_command) +
        sizeof(struct dysymtab_command) +
        sizeof(__text);
    symtabCommand.nsyms = 2; /* number of symbol table entries */
    symtabCommand.stroff = symtabCommand.symoff + sizeof(symtab);
    symtabCommand.strsize = sizeof(__strtab); /* string table size in bytes */

    /* LC_DYSIMTAB */
    struct dysymtab_command dysymtabCommand;
    memset(&dysymtabCommand,0,sizeof(dysymtabCommand));
    dysymtabCommand.cmd = LC_DYSYMTAB;
    dysymtabCommand.cmdsize = sizeof(dysymtabCommand);

    /* Section 64 Header (__TEXT) */
    struct section_64 secText;
    memset(&secText,0,sizeof(secText));
    memcpy(&secText.sectname, "__text", 6);
    memcpy(&secText.segname, "__TEXT", 6); /* segment this section goes in */
    secText.addr = 0LL;          /* memory address of this section */
    secText.size = sizeof(__text); /* size in bytes of this section */
    secText.offset = sizeof(struct mach_header_64) 
        + sizeof(struct segment_command_64) 
        + 1 * sizeof(struct section_64) + sizeof(struct symtab_command)
        + sizeof(struct dysymtab_command);
    secText.align = 4;
    secText.reloff = 0;
    secText.nreloc = 0;
    secText.flags = S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;

    /* LC_SEGMENT64 */
    struct segment_command_64 topSeg;
    memset(&topSeg,0,sizeof(topSeg));

    topSeg.cmd = LC_SEGMENT_64;
    topSeg.nsects = 1;
    topSeg.cmdsize = sizeof(struct segment_command_64) +
                     sizeof(struct section_64) * topSeg.nsects;
    /* topSeg.segname -- not set */
    topSeg.vmaddr = 0;
    topSeg.vmsize = sizeof(__text);
    topSeg.fileoff = sizeof(struct mach_header_64) +
                     sizeof(struct segment_command_64) +
                     sizeof(struct section_64) * topSeg.nsects +
                     sizeof(struct symtab_command) +
                     sizeof(struct dysymtab_command); /* Where text starts */
    topSeg.filesize = topSeg.vmsize;
    topSeg.maxprot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
    topSeg.initprot = topSeg.maxprot;
    topSeg.flags = 0;

    int errno = 0;
    struct mach_header_64 machoHeader;
    memset(&machoHeader, 0, sizeof(machoHeader));

    machoHeader.magic = MH_MAGIC_64;       /* mach magic number identifier */
    machoHeader.cputype = CPU_TYPE_X86_64;  /* cpu specifier */
    machoHeader.cpusubtype = CPU_SUBTYPE_X86_ALL;   /* machine specifier */
    machoHeader.filetype = MH_OBJECT;   /* type of file */
    machoHeader.ncmds = 3;  /* number of load commands */
    machoHeader.sizeofcmds = sizeof(struct segment_command_64) + 
        sizeof(struct section_64) * topSeg.nsects + 
        sizeof(struct symtab_command) +
        sizeof(struct dysymtab_command); /* the size of all the load commands */
        
    machoHeader.flags = 0;      /* flags */
    machoHeader.reserved = 0;   /* reserved */

    FILE * f = fopen("test_macho.obj","w+");

    /* Error! */
    if(!f){
        perror(strerror(errno));
        return errno;
    }

    fwrite(&machoHeader, sizeof(machoHeader), 1, f);
    fwrite(&topSeg, sizeof(topSeg), 1, f);   /* LC_SEGMENT_64 */
    fwrite(&secText, sizeof(secText), 1, f); /* Text Header */
    fwrite(&symtabCommand, sizeof(symtabCommand), 1, f); /* LC_SYMTAB */
    fwrite(&dysymtabCommand, sizeof(dysymtabCommand), 1, f); /* LC_DYSYMTAB */
    fwrite(&__text, sizeof(__text), 1, f); /* Code */
    fwrite(&symtab, sizeof(symtab), 1, f); /* Symbol Table */
    fwrite(&__strtab, sizeof(__strtab), 1, f); /* String List */
    fclose(f);
}
