/****************************************************************
 *                                                              *
 *      Copyright 2007, 2011 Fidelity Information Services, Inc *
 *                                                              *
 *      This source code contains the intellectual property     *
 *      of its copyright holder(s), and is made available       *
 *      under a license.  If you do not know the terms of       *
 *      the license, please stop and do not read further.       *
 *                                                              *
 ****************************************************************/

#define CODEBUF_TYPE	char
#define CALL_SIZE	12
#define EXTRA_INST	0
#define EXTRA_INST_SIZE	0

#define GEN_CALL(func)  { \
    assert(SIZEOF(*code) == SIZEOF(char));                     \
    *code++ = 0x48;				               \
    *code++ = I386_INS_MOV_eAX;		                       \
    *((intptr_t *)code) = (intptr_t)(unsigned char *)func;     \
    code += SIZEOF(intptr_t);				       \
    *code++ = I386_INS_Grp5_Prefix;		               \
    *code++ = 0xd0;				               \
}
