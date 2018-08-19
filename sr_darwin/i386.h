/****************************************************************
 *								*
 *	Copyright 2007 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef I386_H_INCLUDE
#define I386_H_INCLUDE
#define I386_OP(mnemonic,addressoperand,number) I386_INS_ ## mnemonic ## _ ## addressoperand,
enum one_byte_opcode
{
#include "i386_ops.h"
NUM_ONE_BYTE_OPCODES
};

enum two_byte_opcode
{
#include "i386_ops_2b.h"
NUM_TWO_BYTE_OPCODES
};

enum group1_opcode
{
#include "i386_ops_g1.h"
NUM_GROUP_1_OPCODES
};

enum group2_opcode
{
#include "i386_ops_g2.h"
NUM_GROUP_2_OPCODES
};

enum group3_opcode
{
#include "i386_ops_g3.h"
NUM_GROUP_3_OPCODES
};

enum group4_opcode
{
#include "i386_ops_g4.h"
NUM_GROUP_4_OPCODES
};

enum group5_opcode
{
#include "i386_ops_g5.h"
NUM_GROUP_5_OPCODES
};

enum group6_opcode
{
#include "i386_ops_g6.h"
NUM_GROUP_6_OPCODES
};

enum group7_opcode
{
#include "i386_ops_g7.h"
NUM_GROUP_7_OPCODES
};

enum group8_opcode
{
#include "i386_ops_g8.h"
NUM_GROUP_8_OPCODES
};

#define I386_MOD(mnemonic,number) I386_MOD16_ ## mnemonic,
enum mod16
{
#include "i386_mod_16.h"
NUM_16_BIT_MODS
};
#undef I386_MOD

#define I386_MOD(mnemonic,number) I386_MOD32_ ## mnemonic,
enum mod32
{
#include "i386_mod_32.h"
NUM_32_BIT_MODS
};

#define SS_CODE(mnemonic,number) I386_SS_ ## mnemonic,
enum ssval
{
#include "i386_ss.h"
NUM_SS_CODES
};

#define REGDEF(mnemonic,number) I386_REG_ ## mnemonic

#ifdef __x86_64__
enum reg64
{
#include "i386_reg64.h"
NUM_64_BIT_REGISTERS
};
#endif /* __x86_64__ */

enum reg32
{
#include "i386_reg32.h"
NUM_32_BIT_REGISTERS
};

enum reg16
{
#include "i386_reg16.h"
NUM_16_BIT_REGISTERS
};

enum reg8
{
#include "i386_reg8.h"
NUM_8_BIT_REGISTERS
};


typedef struct
{
	unsigned int	r_m:3;
	unsigned int	reg_opcode:3;
	unsigned int	mod:2;
} ModR_M;

typedef struct
{
	unsigned int	base:3;
	unsigned int	index:3;
	unsigned int	ss:2;
} SIB;


#endif
