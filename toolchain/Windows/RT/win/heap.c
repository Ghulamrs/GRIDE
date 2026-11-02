/* heap.c - malloc, calloc, realloc and free over the process heap (c90).
 *
 * Spec: ISO C 7.20.3 and the Microsoft runtime's choices where C leaves one: malloc(0) a unique block
 * rather than a null pointer, realloc(p, 0) the block freed and a null pointer answered, errno ENOMEM
 * when there is no memory. These replace RTS6x's own allocator, which carves a fixed .sysmem region the
 * C6747's linker lays out; on Windows the heap is the process's.
 *
 * The Debug variant (GRT_DEBUG, docs\Runtime_Design.md 4.3) lays a block out as the Microsoft runtime's debug heap
 * does, so that a program reading memory it should not sees the same bytes: a header, then four guard bytes 0xFD on
 * each side of the block, which is filled with 0xCD when it is made (calloc's with zeros) and 0xDD when it is freed.
 * A free that finds a guard overwritten writes what that runtime reports and ends the program as Debug abort does. */
#include <errno.h>
#include <stddef.h>
#include "grt.h"
#include "win32.h"

static HANDLE heap;

static HANDLE grt_process_heap(void)
{
    if (!heap) heap = GetProcessHeap();
    return heap;
}

#if defined(GRT_DEBUG)

#define HEADER 32           /* the block's size first, its front guard in the last four bytes */

static void grt_heap_fill(unsigned char *p, int value, size_t n)
{
    while (n--) *p++ = (unsigned char)value;
}

static int grt_heap_guarded(const unsigned char *p)
{
    return p[0] == 0xFD && p[1] == 0xFD && p[2] == 0xFD && p[3] == 0xFD;
}

static void *grt_heap_make(size_t size, int zero)
{
    unsigned char *p;
    if (size > (size_t)-1 - HEADER - 4) { errno = ENOMEM; return 0; }
    p = (unsigned char *)HeapAlloc(grt_process_heap(), 0, size + HEADER + 4);
    if (!p) { errno = ENOMEM; return 0; }
    *(size_t *)p = size;
    grt_heap_fill(p + HEADER - 4, 0xFD, 4);
    grt_heap_fill(p + HEADER, zero ? 0 : 0xCD, size);
    grt_heap_fill(p + HEADER + size, 0xFD, 4);
    return p + HEADER;
}

static void grt_heap_say(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    __grt_write(2, s, n);
}

/* the block's base, its guards checked: a corrupted one is reported, and the program ends with exit code 3 */
static unsigned char *grt_heap_base(void *block)
{
    unsigned char *p = (unsigned char *)block - HEADER;
    size_t size = *(size_t *)p;
    int front = grt_heap_guarded(p + HEADER - 4);
    if (!front || !grt_heap_guarded(p + HEADER + size)) {
        char address[20];
        const char *digits = "0123456789ABCDEF";
        unsigned long long a = (unsigned long long)block;
        int i;
        for (i = 0; i < 16; i++) address[i] = digits[(a >> (60 - 4 * i)) & 15];
        address[16] = '\n';
        address[17] = 0;
        grt_heap_say(front ? "HEAP CORRUPTION DETECTED: after Normal block at 0x" : "HEAP CORRUPTION DETECTED: before Normal block at 0x");
        grt_heap_say(address);
        grt_heap_say(front ? "CRT detected that the application wrote to memory after end of heap buffer.\n"
                  : "CRT detected that the application wrote to memory before start of heap buffer.\n");
        __rts6x_halt(3);
    }
    return p;
}

void *malloc(size_t size)
{
    return grt_heap_make(size, 0);
}

void *calloc(size_t count, size_t size)
{
    if (size != 0 && count > (size_t)-1 / size) { errno = ENOMEM; return 0; }
    return grt_heap_make(count * size, 1);
}

void free(void *block)
{
    unsigned char *p;
    if (!block) return;
    p = grt_heap_base(block);
    grt_heap_fill((unsigned char *)block, 0xDD, *(size_t *)p);
    HeapFree(grt_process_heap(), 0, p);
}

void *realloc(void *block, size_t size)
{
    unsigned char *p, *q;
    size_t old, i;
    if (!block) return malloc(size);
    if (size == 0) { free(block); return 0; }
    p = grt_heap_base(block);
    old = *(size_t *)p;
    q = (unsigned char *)grt_heap_make(size, 0);
    if (!q) return 0;
    for (i = 0; i < old && i < size; i++) q[i] = ((unsigned char *)block)[i];
    free(block);
    return q;
}

#else

void *malloc(size_t size)
{
    void *p = HeapAlloc(grt_process_heap(), 0, size ? size : 1);
    if (!p) errno = ENOMEM;
    return p;
}

void *calloc(size_t count, size_t size)
{
    void *p;
    if (size != 0 && count > (size_t)-1 / size) { errno = ENOMEM; return 0; }
    p = HeapAlloc(grt_process_heap(), GRT_HEAP_ZERO_MEMORY, count * size ? count * size : 1);
    if (!p) errno = ENOMEM;
    return p;
}

void *realloc(void *block, size_t size)
{
    void *p;
    if (!block) return malloc(size);
    if (size == 0) { HeapFree(grt_process_heap(), 0, block); return 0; }
    p = HeapReAlloc(grt_process_heap(), 0, block, size);
    if (!p) errno = ENOMEM;
    return p;
}

void free(void *block)
{
    if (block) HeapFree(grt_process_heap(), 0, block);
}

#endif
