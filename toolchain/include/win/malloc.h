// malloc.h - the Microsoft runtime's heap calls beyond C's (docs\Runtime_Design.md 9.4, the policy of 2026-11-10).
//
// Not Microsoft's header, and not a copy of one: the prototypes, which agree with the UCRT's, since a program here
// may link either runtime. Both of GRIDE's runtimes define them, grt on Windows and RTS6x on the C6747, and alloca
// is the compilers' own: no library defines it. C's four come from <stdlib.h>, as the UCRT's malloc.h declares them too.
#ifndef _GRIDE_MALLOC_H
#define _GRIDE_MALLOC_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// The size of the block malloc, calloc or realloc gave. **The usable size**: at least what was asked for, and on
// either runtime's Release heap the whole of the block it was given (a 10-byte block's _msize is 16), where the
// UCRT answers the size asked for - a recorded deviation (section 9.4). The Debug heap answers the size asked for.
size_t _msize(void *block);
// A block of size bytes at a multiple of alignment, a power of two, which only _aligned_free takes back.
void *_aligned_malloc(size_t size, size_t alignment);
void _aligned_free(void *block);

// **size bytes on the stack**, the compilers' own (as cl's is: the UCRT defines no _alloca either). cpp11 and c90
// lower the call where it stands, on x86_64-windows and tms6747: the stack pointer moves down by size rounded up
// (16 bytes there, 8 on the C6747), a page at a time through __chkstk on Windows, and the block lasts until the
// function that called it returns - not to the end of a block, and a loop that calls it keeps every one. Nothing
// reports a failure: on Windows a stack the size does not fit in ends the program, as it does on Microsoft's runtime,
// and the C6747's stack has no guard below it. Not in a function with a catch, which refuses it (section 9.4).
void *_alloca(size_t size);
#define alloca _alloca

#ifdef __cplusplus
}
#endif

#endif