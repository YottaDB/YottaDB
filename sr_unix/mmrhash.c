/****************************************************************
 *								*
 * Copyright 2011, 2014 Fidelity Information Services, Inc	*
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

/*-----------------------------------------------------------------------------
 * MurmurHash3 was written by Austin Appleby, and is placed in the public
 * domain. The author hereby disclaims copyright to this source code.
 *
 * Note - The x86 and x64 versions do _not_ produce the same results, as the
 * algorithms are optimized for their respective platforms. You can still
 * compile and run any of them on any platform, but your performance with the
 * non-native version will be less than optimal.
 *
 * This version converted to C for use in GT.M by FIS.
 *-----------------------------------------------------------------------------*/

#include "mdef.h"

#include "gtm_string.h"

#include "mmrhash.h"

/*
 * Since this code is largely third-party, its form is somewhat different than
 * other code in GT.M. This is intentional, at least for the initial version,
 * in order to allow for comparison with the original. This may be cleaned up
 * in future versions, as no revisions to the original public domain code are
 * expected. The original code is hosted at http://code.google.com/p/smhasher/ .
 *
 * Note also that GT.M is currently only using the 32 bit hash function,
 * MurmurHash3_x86_32(). The 128 bit functions are retained for completeness
 * and possible future use.
 *
 */

#if 0
#define	FORCE_INLINE __attribute__((always_inline))
#else
#define	FORCE_INLINE
#endif


static uint4 fmix ( uint4 h );

static gtm_uint8 fmix64 ( gtm_uint8 k );

#define ROTL32(X,Y)	(((X) << (Y)) | ((X) >> (32 - (Y))))
#define ROTL64(X,Y)	(((X) << (Y)) | ((X) >> (64 - (Y))))

#define BIG_CONSTANT(x) (x##LLU)

#define GETBLOCK(p,i) (((const uint4 *)p)[(int)i])

#define GETBLOCK64(p,i) (((const gtm_uint8 *)p)[(int)i])

#ifdef BIGENDIAN
#	if defined(__GNUC__) && (__GNUC__>4 || (__GNUC__==4 && __GNUC_MINOR__>=3))
#	define UI64_TO_LE(X) (__builtin_bswap64(*((uint64_t*)&(X))))
#	else
#	define UI64_TO_LE(X) (X) = ((((X) & BIG_CONSTANT(0xff)) << 56)								\
					| (((X) & BIG_CONSTANT(0xff00)) << 40)							\
					| (((X) & BIG_CONSTANT(0xff0000)) << 24)						\
					| (((X) & BIG_CONSTANT(0xff000000)) << 8)						\
					| (((X) & BIG_CONSTANT(0xff00000000)) >> 8)						\
					| (((X) & BIG_CONSTANT(0xff0000000000)) >> 24)						\
					| (((X) & BIG_CONSTANT(0xff000000000000)) >> 40)					\
					| (((X) & BIG_CONSTANT(0xff00000000000000)) >> 56))
#	endif
#else
#define UI64_TO_LE(X)			/* nothing */
#endif

/*-----------------------------------------------------------------------------
 * Finalization mix - force all bits of a hash block to avalanche            */

static FORCE_INLINE uint4 fmix ( uint4 h )
{
  h ^= h >> 16;
  h *= 0x85ebca6b;
  h ^= h >> 13;
  h *= 0xc2b2ae35;
  h ^= h >> 16;

  return h;
}

/*----------*/

static FORCE_INLINE gtm_uint8 fmix64 ( gtm_uint8 k )
{
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xff51afd7ed558ccd);
  k ^= k >> 33;
  k *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
  k ^= k >> 33;

  return k;
}

/*-----------------------------------------------------------------------------*/

void MurmurHash3_x86_32 ( const void * key, int len,
                          uint4 seed, void * out )
{
  int i;
  const unsigned char * tail;
  const uint4 * blocks;
  const int nblocks = len / 4;
  static char	*buff;
  static int	bufflen;

  uint4 h1 = seed;

  uint4 c1 = 0xcc9e2d51;
  uint4 c2 = 0x1b873593;
  uint4 k1;

  /*----------
   * body   */
# ifndef UNALIGNED_ACCESS_SUPPORTED
  /* Murmur3 hash works only on 4-byte aligned keys so align key to avoid unaligned access error
   * messages from an architecture that does not support it.
   */
  if (len && (0 != ((UINTPTR_T)key % 4)))
  {	/* make buffer 4-byte aligned */
  	if (bufflen < len)
	{
  		if (NULL != buff)
		{
			assert(bufflen);
			free(buff);
		}
		bufflen = len * 2;
		buff = malloc(bufflen);
	}
	assert(bufflen >= len);
	memcpy(buff, key, len);
	key = (const unsigned char*)buff;
  }
# endif
  blocks = (const uint4 *)((char *)key + nblocks*4);
  for(i = -nblocks; i; i++)
  {
    k1 = GETBLOCK(blocks,i);

    k1 *= c1;
    k1 = ROTL32(k1,15);
    k1 *= c2;

    h1 ^= k1;
    h1 = ROTL32(h1,13);
    h1 = h1*5+0xe6546b64;
  }

  /*----------
   * tail   */

  tail = (const unsigned char*)blocks;

  k1 = 0;

  /* The shifts below assume little endian, so the handling of the tail block is inconsistent with normal blocks
   * on big endian systems. However, we are already using this version and we don't want to change the resulting
   * hashes, so leave it alone.
   */
  switch(len & 3)
  {
  case 3: k1 ^= tail[2] << 16;
  case 2: k1 ^= tail[1] << 8;
  case 1: k1 ^= tail[0];
          k1 *= c1; k1 = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
  };

  /*----------
   * finalization  */

  h1 ^= len;

  h1 = fmix(h1);

  *(uint4*)out = h1;
}

/*-----------------------------------------------------------------------------*/

void MurmurHash3_x86_128 ( const void * key, const int len,
                           uint4 seed, void * out )
{
  int i;
  const unsigned char * data = (const unsigned char*)key;
  const unsigned char * tail;
  const int nblocks = len / 16;
  static char	*buff;
  static int	bufflen;

  uint4 h1 = seed;
  uint4 h2 = seed;
  uint4 h3 = seed;
  uint4 h4 = seed;

  uint4 c1 = 0x239b961b;
  uint4 c2 = 0xab0e9789;
  uint4 c3 = 0x38b34ae5;
  uint4 c4 = 0xa1e38b93;

  uint4 k1;
  uint4 k2;
  uint4 k3;
  uint4 k4;

  /*----------
   * body   */
# ifndef UNALIGNED_ACCESS_SUPPORTED
  /* Murmur3 hash works only on 4-byte aligned keys so align key to avoid unaligned access error
   * messages from an architecture that does not support it.
   */
  if (len && (0 != ((UINTPTR_T)key % 4)))
  {	/* make buffer 4-byte aligned */
  	if (bufflen < len)
	{
  		if (NULL != buff)
		{
			assert(bufflen);
			free(buff);
		}
		bufflen = len * 2;
		buff = malloc(bufflen);
	}
	assert(bufflen >= len);
	memcpy(buff, key, len);
	data = (const unsigned char*)buff;
  }
# endif

  const uint4 * blocks = (const uint4 *)(data + nblocks*16);

  for(i = -nblocks; i; i++)
  {
    k1 = GETBLOCK(blocks,i*4+0);
    k2 = GETBLOCK(blocks,i*4+1);
    k3 = GETBLOCK(blocks,i*4+2);
    k4 = GETBLOCK(blocks,i*4+3);

    k1 *= c1; k1  = ROTL32(k1,15); k1 *= c2; h1 ^= k1;

    h1 = ROTL32(h1,19); h1 += h2; h1 = h1*5+0x561ccd1b;

    k2 *= c2; k2  = ROTL32(k2,16); k2 *= c3; h2 ^= k2;

    h2 = ROTL32(h2,17); h2 += h3; h2 = h2*5+0x0bcaa747;

    k3 *= c3; k3  = ROTL32(k3,17); k3 *= c4; h3 ^= k3;

    h3 = ROTL32(h3,15); h3 += h4; h3 = h3*5+0x96cd1c35;

    k4 *= c4; k4  = ROTL32(k4,18); k4 *= c1; h4 ^= k4;

    h4 = ROTL32(h4,13); h4 += h1; h4 = h4*5+0x32ac3b17;
  }

  /*----------
   * tail   */

  tail = (const unsigned char*)(data + nblocks*16);

  k1 = 0;
  k2 = 0;
  k3 = 0;
  k4 = 0;

  /* The shifts below assume little endian, so the handling of the tail block is inconsistent with normal blocks
   * on big endian systems. If we start using this routine, we should decide whether to fix it or not.
   */
  switch(len & 15)
  {
  case 15: k4 ^= tail[14] << 16;
  case 14: k4 ^= tail[13] << 8;
  case 13: k4 ^= tail[12] << 0;
           k4 *= c4; k4  = ROTL32(k4,18); k4 *= c1; h4 ^= k4;

  case 12: k3 ^= tail[11] << 24;
  case 11: k3 ^= tail[10] << 16;
  case 10: k3 ^= tail[ 9] << 8;
  case  9: k3 ^= tail[ 8] << 0;
           k3 *= c3; k3  = ROTL32(k3,17); k3 *= c4; h3 ^= k3;

  case  8: k2 ^= tail[ 7] << 24;
  case  7: k2 ^= tail[ 6] << 16;
  case  6: k2 ^= tail[ 5] << 8;
  case  5: k2 ^= tail[ 4] << 0;
           k2 *= c2; k2  = ROTL32(k2,16); k2 *= c3; h2 ^= k2;

  case  4: k1 ^= tail[ 3] << 24;
  case  3: k1 ^= tail[ 2] << 16;
  case  2: k1 ^= tail[ 1] << 8;
  case  1: k1 ^= tail[ 0] << 0;
           k1 *= c1; k1  = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
  };

  /*----------
   * finalization  */

  h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;

  h1 += h2; h1 += h3; h1 += h4;
  h2 += h1; h3 += h1; h4 += h1;

  h1 = fmix(h1);
  h2 = fmix(h2);
  h3 = fmix(h3);
  h4 = fmix(h4);

  h1 += h2; h1 += h3; h1 += h4;
  h2 += h1; h3 += h1; h4 += h1;

  ((uint4*)out)[0] = h1;
  ((uint4*)out)[1] = h2;
  ((uint4*)out)[2] = h3;
  ((uint4*)out)[3] = h4;
}

/*-----------------------------------------------------------------------------*/

void MurmurHash3_x64_128 ( const void * key, const int len,
                           const uint4 seed, void * out )
{
  int i;
  const unsigned char * data = (const unsigned char*)key;
  const unsigned char * tail;
  const int nblocks = len / 16;
  static char	*buff;
  static int	bufflen;

  gtm_uint8 h1 = seed;
  gtm_uint8 h2 = seed;

  gtm_uint8 c1 = BIG_CONSTANT(0x87c37b91114253d5);
  gtm_uint8 c2 = BIG_CONSTANT(0x4cf5ad432745937f);

  gtm_uint8 k1;
  gtm_uint8 k2;

  /*----------
   * body   */
# ifndef UNALIGNED_ACCESS_SUPPORTED
  /* Murmur3 hash works only on 4-byte aligned keys so align key to avoid unaligned access error
   * messages from an architecture that does not support it.
   */
  if (len && (0 != ((UINTPTR_T)key % 8)))
  {	/* make buffer 4-byte aligned */
  	if (bufflen < len)
	{
  		if (NULL != buff)
		{
			assert(bufflen);
			free(buff);
		}
		bufflen = len * 2;
		buff = malloc(bufflen);
	}
	assert(bufflen >= len);
	memcpy(buff, key, len);
	data = (const unsigned char*)buff;
  }
# endif

  const gtm_uint8 * blocks = (const gtm_uint8 *)(data);

  for(i = 0; i < nblocks; i++)
  {
    k1 = GETBLOCK64(blocks,i*2+0);
    k2 = GETBLOCK64(blocks,i*2+1);

    UI64_TO_LE(k1);
    UI64_TO_LE(k2);

    k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;

    h1 = ROTL64(h1,27); h1 += h2; h1 = h1*5+0x52dce729;

    k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

    h2 = ROTL64(h2,31); h2 += h1; h2 = h2*5+0x38495ab5;
  }

  /*----------
   * tail   */

  tail = (const unsigned char*)(data + nblocks*16);

  k1 = 0;
  k2 = 0;

  /* The shifts below originally assumed little endian, so the handling of the tail block was inconsistent with normal blocks
   * on big endian systems. Since we haven't used this routine previously, fix it here instead of making the progressive
   * version consistent with the broken implementation.
   */
  switch(len & 15)
  {
  case 15: k2 ^= ((gtm_uint8)tail[14]) << 48;
  case 14: k2 ^= ((gtm_uint8)tail[13]) << 40;
  case 13: k2 ^= ((gtm_uint8)tail[12]) << 32;
  case 12: k2 ^= ((gtm_uint8)tail[11]) << 24;
  case 11: k2 ^= ((gtm_uint8)tail[10]) << 16;
  case 10: k2 ^= ((gtm_uint8)tail[ 9]) << 8;
  case  9: k2 ^= ((gtm_uint8)tail[ 8]) << 0;
           k2 *= c2; k2  = ROTL64(k2,33); k2 *= c1; h2 ^= k2;

  case  8: k1 ^= ((gtm_uint8)tail[ 7]) << 56;
  case  7: k1 ^= ((gtm_uint8)tail[ 6]) << 48;
  case  6: k1 ^= ((gtm_uint8)tail[ 5]) << 40;
  case  5: k1 ^= ((gtm_uint8)tail[ 4]) << 32;
  case  4: k1 ^= ((gtm_uint8)tail[ 3]) << 24;
  case  3: k1 ^= ((gtm_uint8)tail[ 2]) << 16;
  case  2: k1 ^= ((gtm_uint8)tail[ 1]) << 8;
  case  1: k1 ^= ((gtm_uint8)tail[ 0]) << 0;
           k1 *= c1; k1  = ROTL64(k1,31); k1 *= c2; h1 ^= k1;
  };

  /*----------
   * finalization  */

  h1 ^= len; h2 ^= len;

  h1 += h2;
  h2 += h1;

  h1 = fmix64(h1);
  h2 = fmix64(h2);

  h1 += h2;
  h2 += h1;

  ((gtm_uint8*)out)[0] = h1;
  ((gtm_uint8*)out)[1] = h2;
}

/*******************************************************************************
 * Progressive 128-bit MurmurHash3
 *******************************************************************************/

#define C1 BIG_CONSTANT(0x87c37b91114253d5)
#define C2 BIG_CONSTANT(0x4cf5ad432745937f)
#define M1 0x52dce729
#define M2 0x38495ab5

#define LSHIFT_BYTES_SAFE(LHS, RHS)	((LHS) << ((RHS) * 8))
#define RSHIFT_BYTES_SAFE(LHS, RHS)	((LHS) >> ((RHS) * 8))
#define LSHIFT_BYTES(LHS, RHS)		((RHS < SIZEOF(LHS)) ? LSHIFT_BYTES_SAFE(LHS, RHS) : 0)
#define RSHIFT_BYTES(LHS, RHS)		((RHS < SIZEOF(LHS)) ? RSHIFT_BYTES_SAFE(LHS, RHS) : 0)

#if !defined(BIGENDIAN)

/* Little Endian */

#define ADD_BYTES(STATEPTR, KEY, COUNT)										\
MBSTART {													\
	int			ab_count, ab_offset, ab_carry_bytes;						\
	const unsigned char	*ab_key, *ab_ptr;								\
	gtm_uint8		c1, c2;										\
														\
	assert((STATEPTR)->carry_bytes + (COUNT) >= 0);								\
	assert((STATEPTR)->carry_bytes + (COUNT) <= 16);							\
	ab_count = (COUNT);											\
	ab_carry_bytes = (STATEPTR)->carry_bytes;								\
	ab_offset = ab_carry_bytes + ab_count;									\
	ab_key = (KEY);												\
	ab_ptr = ab_key + ab_count - 1;										\
	c1 = (STATEPTR)->c.one;											\
	c2 = (STATEPTR)->c.two;											\
	assert((ab_carry_bytes != 0) || ((c1 == 0) && (c2 == 0)));						\
	for (; (ab_offset > 8) && (ab_ptr >= ab_key); ab_ptr--, ab_offset--)					\
		c2 |= LSHIFT_BYTES_SAFE((gtm_uint8)*ab_ptr, (ab_offset - 9));					\
	for (; ab_ptr >= ab_key; ab_ptr--, ab_offset--)								\
		c1 |= LSHIFT_BYTES_SAFE((gtm_uint8)*ab_ptr, (ab_offset - 1));					\
	assert(((ab_carry_bytes + ab_count) != 0) || ((c1 == 0) && (c2 == 0)));					\
	assert((ab_count != 0) || (((STATEPTR)->c.one == c1) && ((STATEPTR)->c.two == c2)));			\
	(STATEPTR)->c.one = c1;											\
	(STATEPTR)->c.two = c2;											\
	(STATEPTR)->carry_bytes = ab_carry_bytes + ab_count;							\
} MBEND

#define GET_BLOCK(STATEPTR, KEY, K1, K2)									\
MBSTART {													\
	int		gb_carry_bytes;										\
	gtm_uint8	gb_m1, gb_m2, gb_k1, gb_k2, gb_c1, gb_c2;						\
														\
	assert(((STATEPTR)->carry_bytes != 0) || (((STATEPTR)->c.one == 0) && ((STATEPTR)->c.two == 0)));	\
	assert((STATEPTR)->carry_bytes < 16);									\
	gb_carry_bytes = (STATEPTR)->carry_bytes;								\
	gb_m1 = ((gtm_uint8 *)(KEY))[0];									\
	gb_m2 = ((gtm_uint8 *)(KEY))[1];									\
	if (0 == gb_carry_bytes)										\
	{													\
		(K1) = gb_m1;											\
		(K2) = gb_m2;											\
	}													\
	else if (8 == gb_carry_bytes)										\
	{													\
		gb_k1 = (STATEPTR)->c.one;									\
		gb_k2 = gb_m1;											\
		gb_c1 = gb_m2; 											\
		/* gb_c2 = 0; */										\
		(K1) = gb_k1;											\
		(K2) = gb_k2;											\
		(STATEPTR)->c.one = gb_c1;									\
		assert(0 == (STATEPTR)->c.two);									\
	}													\
	else if (gb_carry_bytes < 8)										\
	{													\
		gb_k1 = (STATEPTR)->c.one | LSHIFT_BYTES_SAFE(gb_m1, gb_carry_bytes);				\
		gb_k2 = RSHIFT_BYTES_SAFE(gb_m1, (8 - gb_carry_bytes))				 		\
			| LSHIFT_BYTES_SAFE(gb_m2, gb_carry_bytes);						\
		gb_c1 = RSHIFT_BYTES_SAFE(gb_m2, (8 - gb_carry_bytes));						\
		/* gb_c2 = 0; */										\
		(K1) = gb_k1;											\
		(K2) = gb_k2;											\
		(STATEPTR)->c.one = gb_c1;									\
		assert(0 == (STATEPTR)->c.two);									\
	}													\
	else {													\
		gb_k1 = (STATEPTR)->c.one;									\
		gb_k2 = (STATEPTR)->c.two									\
			| LSHIFT_BYTES_SAFE(gb_m1, (gb_carry_bytes - 8));					\
		gb_c1 = RSHIFT_BYTES_SAFE(gb_m1, (16 - gb_carry_bytes))						\
			| LSHIFT_BYTES_SAFE(gb_m2, (gb_carry_bytes - 8));					\
		gb_c2 = RSHIFT_BYTES_SAFE(gb_m2, (16 - gb_carry_bytes));					\
		(K1) = gb_k1;											\
		(K2) = gb_k2;											\
		(STATEPTR)->c.one = gb_c1;									\
		(STATEPTR)->c.two = gb_c2;									\
	}													\
} MBEND

#else

/* Big Endian */

#define ADD_BYTES(STATEPTR, KEY, COUNT)										\
MBSTART {													\
	int			ab_count, ab_offset, ab_carry_bytes;						\
	const unsigned char	*ab_key, *ab_ptr;								\
	gtm_uint8		c1, c2;										\
														\
	assert((STATEPTR)->carry_bytes + (COUNT) >= 0);								\
	assert((STATEPTR)->carry_bytes + (COUNT) <= 16);							\
	ab_count = (COUNT);											\
	ab_carry_bytes = (STATEPTR)->carry_bytes;								\
	ab_offset = ab_carry_bytes + ab_count;									\
	ab_key = (KEY);												\
	ab_ptr = ab_key + ab_count - 1;										\
	c1 = (STATEPTR)->c.one;											\
	c2 = (STATEPTR)->c.two;											\
	assert((ab_carry_bytes != 0) || ((c1 == 0) && (c2 == 0)));						\
	for (; (ab_offset > 8) && (ab_ptr >= ab_key); ab_ptr--, ab_offset--)					\
		c2 |= LSHIFT_BYTES_SAFE((gtm_uint8)*ab_ptr, (16 - ab_offset));					\
	for (; ab_ptr >= ab_key; ab_ptr--, ab_offset--)								\
		c1 |= LSHIFT_BYTES_SAFE((gtm_uint8)*ab_ptr, (8 - ab_offset));					\
	assert(((ab_carry_bytes + ab_count) != 0) || ((c1 == 0) && (c2 == 0)));					\
	assert((ab_count != 0) || (((STATEPTR)->c.one == c1) && ((STATEPTR)->c.two == c2)));			\
	(STATEPTR)->c.one = c1;											\
	(STATEPTR)->c.two = c2;											\
	(STATEPTR)->carry_bytes = ab_carry_bytes + ab_count;							\
} MBEND

#define GET_BLOCK(STATEPTR, KEY, K1, K2)									\
MBSTART {													\
	int		gb_carry_bytes;										\
	gtm_uint8	gb_m1, gb_m2, gb_k1, gb_k2, gb_c1, gb_c2;						\
														\
	assert(((STATEPTR)->carry_bytes != 0) || (((STATEPTR)->c.one == 0) && ((STATEPTR)->c.two == 0)));	\
	gb_carry_bytes = (STATEPTR)->carry_bytes;								\
	gb_m1 = ((gtm_uint8 *)(KEY))[0];									\
	gb_m2 = ((gtm_uint8 *)(KEY))[1];									\
	if (0 == gb_carry_bytes)										\
	{													\
		(K1) = gb_m1;											\
		(K2) = gb_m2;											\
	}													\
	else if (8 == gb_carry_bytes)										\
	{													\
		gb_k1 = (STATEPTR)->c.one;									\
		gb_k2 = gb_m1;									 		\
		gb_c1 = gb_m2;											\
		/* gb_c2 = 0; */										\
		(K1) = gb_k1;											\
		(K2) = gb_k2;											\
		(STATEPTR)->c.one = gb_c1;									\
		assert(0 == (STATEPTR)->c.two);									\
	}													\
	else if ( gb_carry_bytes < 8)										\
	{													\
		gb_k1 = (STATEPTR)->c.one | RSHIFT_BYTES_SAFE(gb_m1, gb_carry_bytes);				\
		gb_k2 = LSHIFT_BYTES_SAFE(gb_m1, (8 - gb_carry_bytes))				 		\
			| RSHIFT_BYTES_SAFE(gb_m2, gb_carry_bytes);						\
		gb_c1 = LSHIFT_BYTES_SAFE(gb_m2, (8 - gb_carry_bytes));						\
		/* gb_c2 = 0; */										\
		(K1) = gb_k1;											\
		(K2) = gb_k2;											\
		(STATEPTR)->c.one = gb_c1;									\
		assert(0 == (STATEPTR)->c.two);									\
	}													\
	else {													\
		gb_k1 = (STATEPTR)->c.one;									\
		gb_k2 = (STATEPTR)->c.two									\
			| RSHIFT_BYTES_SAFE(gb_m1, (gb_carry_bytes - 8));					\
		gb_c1 = LSHIFT_BYTES_SAFE(gb_m1, (16 - gb_carry_bytes))						\
			| RSHIFT_BYTES_SAFE(gb_m2, (gb_carry_bytes - 8));					\
		gb_c2 = LSHIFT_BYTES_SAFE(gb_m2, (16 - gb_carry_bytes));					\
		(K1) = gb_k1;											\
		(K2) = gb_k2;											\
		(STATEPTR)->c.one = gb_c1;									\
		(STATEPTR)->c.two = gb_c2;									\
	}													\
} MBEND

#endif

#define INTEGRATE_K1(K1, H1)											\
MBSTART {													\
	(K1) *= C1;												\
	(K1) = ROTL64((K1), 31);										\
	(K1) *= C2;												\
	(H1) ^= (K1);												\
} MBEND

#define INTEGRATE_K2(K2, H2)											\
MBSTART {													\
	(K2) *= C2;												\
	(K2) = ROTL64((K2), 33);										\
	(K2) *= C1;												\
	(H2) ^= (K2);												\
} MBEND

#define PROCESS_BLOCK(K1, K2, H1, H2)										\
MBSTART {													\
	UI64_TO_LE(K1);												\
	UI64_TO_LE(K2);												\
	INTEGRATE_K1(K1, H1);											\
	(H1) = ROTL64((H1), 27);										\
	(H1) += (H2);												\
	(H1) = (H1) * 5 + M1;											\
	INTEGRATE_K2(K2, H2);											\
	(H2) = ROTL64((H2), 31);										\
	(H2) += (H1);												\
	(H2) = (H2) * 5 + M2;											\
} MBEND

#define PROCESS_BYTES(STATEPTR, KEY, LEN)									\
MBSTART {													\
	int pb_carry_fill = 0;											\
														\
	assert((LEN) < 16);											\
	if ((STATEPTR)->carry_bytes + (LEN) >= 16)								\
	{													\
		/* fill the carry field, process it as a block, then clear it */				\
		pb_carry_fill = 16 - (STATEPTR)->carry_bytes;							\
		ADD_BYTES((STATEPTR), (KEY), pb_carry_fill);							\
		assert((STATEPTR)->carry_bytes == 16);								\
		PROCESS_BLOCK((STATEPTR)->c.one, (STATEPTR)->c.two, (STATEPTR)->h.one, (STATEPTR)->h.two);	\
		(STATEPTR)->c.one = (STATEPTR)->c.two = 0;							\
		(STATEPTR)->carry_bytes = 0;									\
	}													\
	ADD_BYTES((STATEPTR), (KEY) + pb_carry_fill, (LEN) - pb_carry_fill);					\
} MBEND


/* This is an endian-independent 16-byte murmur hash function */
void gtmmrhash_128(const void *key, int len, uint4 seed, gtm_uint16 *out)
{
	hash128_state_t state;

	HASH128_STATE_INIT(state, seed);
	assert((state.carry_bytes == 0) && (state.c.one == 0) && (state.c.two == 0));
	gtmmrhash_128_ingest(&state, key, len);
	gtmmrhash_128_result(&state, len, out);
}

/* This is the same as gtmmrhash_128 (i.e. is endian independent) except that it generates a 4-byte hash
 * (needed e.g. by STR_HASH macro). To avoid the overhead of an extra function call, we duplicate the
 * code of "gtmmmrhash_128" here.
 */
void gtmmrhash_32(const void *key, int len, uint4 seed, uint4 *out4)
{
	hash128_state_t state;
	gtm_uint16	out16;

	HASH128_STATE_INIT(state, seed);
	assert((state.carry_bytes == 0) && (state.c.one == 0) && (state.c.two == 0));
	gtmmrhash_128_ingest(&state, key, len);
	gtmmrhash_128_result(&state, len, &out16);
	*out4 = (uint4)out16.one;
}

void gtmmrhash_128_ingest(hash128_state_t *state, const void *key, int len)
{
	int			i;
	gtm_uint8		k1, k2;
	const unsigned char	*keyptr;

	if (0 == len)
		return;

	keyptr = key;
#	if !(defined(__i386) || defined(__x86_64__) || defined(_AIX))
	/* determine the number of bytes to consume to reach 64-bit (8 byte) alignment */
	i = (0x8 - ((UINTPTR_T)keyptr & 0x7)) & 0x7;
	if (i > len)
		i = len;
	if (i > 0)
	{
		PROCESS_BYTES(state, keyptr, i);
		keyptr += i;
		len -= i;
	}
#	endif
	for (; len >= 16; len -= 16, keyptr += 16)
	{
		GET_BLOCK(state, keyptr, k1, k2);
		PROCESS_BLOCK(k1, k2, state->h.one, state->h.two);
	}
	if (len > 0)
		PROCESS_BYTES(state, keyptr, len);
}

void gtmmrhash_128_result(hash128_state_t *state, uint4 total_len, gtm_uint16 *out)
{
	gtm_uint8		k1, k2, h1, h2;

	k1 = state->c.one;
	k2 = state->c.two;
	h1 = state->h.one;
	h2 = state->h.two;

	UI64_TO_LE(k1);
	UI64_TO_LE(k2);
	if (state->carry_bytes != 0)
	{
		if (state->carry_bytes > 8)
		{
			INTEGRATE_K2(k2, h2);
		}
		INTEGRATE_K1(k1, h1);
	}

	h1 ^= total_len;
	h2 ^= total_len;
	h1 += h2;
	h2 += h1;
	h1 = fmix64(h1);
	h2 = fmix64(h2);
	h1 += h2;
	h2 += h1;

	out->one = h1;
	out->two = h2;
}

void gtmmrhash_128_bytes(const gtm_uint16 *hash, unsigned char *out)
{
#	ifdef BIGENDIAN
#	define EXTRACT_BYTE(X, N)	(((uint64_t)(X) & ((uint64_t)0xff << (N) * 8)) >> (N) * 8)
	out[0] = EXTRACT_BYTE(hash->one, 0);
	out[1] = EXTRACT_BYTE(hash->one, 1);
	out[2] = EXTRACT_BYTE(hash->one, 2);
	out[3] = EXTRACT_BYTE(hash->one, 3);
	out[4] = EXTRACT_BYTE(hash->one, 4);
	out[5] = EXTRACT_BYTE(hash->one, 5);
	out[6] = EXTRACT_BYTE(hash->one, 6);
	out[7] = EXTRACT_BYTE(hash->one, 7);
	out[8] = EXTRACT_BYTE(hash->two, 0);
	out[9] = EXTRACT_BYTE(hash->two, 1);
	out[10] = EXTRACT_BYTE(hash->two, 2);
	out[11] = EXTRACT_BYTE(hash->two, 3);
	out[12] = EXTRACT_BYTE(hash->two, 4);
	out[13] = EXTRACT_BYTE(hash->two, 5);
	out[14] = EXTRACT_BYTE(hash->two, 6);
	out[15] = EXTRACT_BYTE(hash->two, 7);
#	else
	((gtm_uint8 *)out)[0] = hash->one;
	((gtm_uint8 *)out)[1] = hash->two;
#	endif
}

void gtmmrhash_128_hex(const gtm_uint16 *hash, unsigned char *out)
{
	int			i;
	unsigned char		bytes[16], n;

	gtmmrhash_128_bytes(hash, bytes);
	for (i = 0; i < 16; i++)
	{
		n = bytes[i] & 0xf;
		out[i * 2 + 1] = (n < 10) ? (n + '0') : (n - 10 + 'a');
		n = (bytes[i] >> 4) & 0xf;
		out[i * 2] = (n < 10) ? (n + '0') : (n - 10 + 'a');
	}
}


/*******************************************************************************/

/*-----------------------------------------------------------------------------*/

