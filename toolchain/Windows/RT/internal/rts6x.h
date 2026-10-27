// Spec: ISO C 7.19.1 (FILE); RTS6x's internal\rts6x.h, for x86_64-windows. GRIDE's runtime compiles RTS6x's
// C library with this folder first on the include path, so "rts6x.h" is this file there. On Windows the
// compilers' <stdio.h> leaves FILE incomplete (a program reaches the streams through __acrt_iob_func), so the
// struct is the runtime's own to lay out: RTS6x's fields first, in RTS6x's order, then the Windows stream
// layer's (c\Stream*.cpp).
#ifndef RTS6X_H
#define RTS6X_H
#include <stdio.h>
struct _IO_FILE {
    int fd;
    // Reading: buf..bufend holds raw bytes from the descriptor, pos the next one to give. Writing: buf..pos
    // holds the bytes to write, already translated, and bufend is buf. buff_stop ends the buffer.
    unsigned char *buf, *pos, *bufend, *buff_stop;
    unsigned int flags;
    int slot;                       // the stream's number: its place in __grt_streams
    void *lock;                     // an SRWLOCK, zero to start: held for each call once there are threads
};
// _ftable[]: stdin, stdout and stderr, as on the C6747. The other streams are allocated as fopen needs them
// (up to 512, the Microsoft runtime's limit) and kept for reuse: c\StreamOpen.cpp.
#define RTS6X_FTABLE_COUNT     3
#define GRT_STREAMS            512
extern "C" FILE _ftable[RTS6X_FTABLE_COUNT];
extern "C" FILE *__grt_streams[GRT_STREAMS];
#endif
