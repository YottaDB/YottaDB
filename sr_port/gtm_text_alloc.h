/****************************************************************
 *								*
 *	Copyright 2007, 2011 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef GTM_TEXT_ALLOC_H
#define GTM_TEXT_ALLOC_H

/* States a storage element may be in (which need to be different from malloc states). */
enum TextState {TextAllocated = 0x43, TextFree = 0x34};

/* Each allocated block (in the mmap build) has the following structure. The actual address
   returned to the user for allocation and supplied by the user for release
   is actually the storage beginning at the 'userStorage.userStart' area.
   This holds true even for storage that is truely mmap'd.
*/
typedef struct textElemStruct
{	/* This flavor of header is 16 bytes. This is required on IA64 and we have just adopted it for
	   the other platforms as well as it is a minor expense given the sizes of chunks we are using
	*/
	int		queueIndex;			/* Index into TwoTable for this size of element */
	enum TextState	state;				/* State of this block */
	unsigned int	realLen;			/* Real (total) length of allocation */
	int		filler;
	union						/* The links are used only when element is free */
	{
		struct					/* Free block information */
		{
			struct	textElemStruct	*fPtr;	/* Next storage element on free queue */
			struct	textElemStruct	*bPtr;	/* Previous storage element on free queue */
		} links;
		unsigned char	userStart;		/* First byte of user useable storage */
	} userStorage;
} textElem;

/* The below macros are used to allocate and release areas of storage that will be used
   to contain executable code. Traditionally, GTM has just used malloc() and free() and
   placed this code on the heap but some systems now protect against that (noteably
   Linux). On those platforms (and any others we choose to separate the executable storage
   pools from the regular heap storage), we use gtm_text_alloc() and gtm_text_free() to
   allocate and free executable storage. These modules use mmap() and munmap() for this
   purpose.

   Note that while use of the mmap() interface for executable storage does have some
   potential advantages for security (and as noted above, is required for some platforms
   to even function, because of the page aligned granularity of its requests, storage usage
   with gtm_text_alloc() is not as efficient as it is with regular heap based storage. For
   this reason, we only use this method on the required platforms rather than all. Replaceing
   the algorithms in gtm_text_alloc.c with ones not based on the buddy system could
   potentially alleviate these efficiency differences.
*/
#if defined(__linux__) || defined(__osf__) || defined(__MVS__) || defined(__CYGWIN__) || defined(__APPLE__)
#  define GTM_TEXT_ALLOC(x) gtm_text_alloc(x)
#  define GTM_TEXT_FREE(x) gtm_text_free(x)
void *gtm_text_alloc(size_t size);
void gtm_text_free(void *addr);
void printAllocInfo(void);
#  define COMP_GTA	/* Build gtm_text_alloc() module */
# else
#  define GTM_TEXT_ALLOC(x) gtm_malloc(x)
#  define GTM_TEXT_FREE(x) gtm_free(x)
#endif

#endif /* GTM_TEXT_ALLOC_H */
