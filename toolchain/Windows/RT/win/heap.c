/* heap.c - malloc, calloc, realloc and free over the process heap (c90).
 *
 * Spec: ISO C 7.20.3 and the Microsoft runtime's choices where C leaves one: malloc(0) a unique block
 * rather than a null pointer, realloc(p, 0) the block freed and a null pointer answered, errno ENOMEM
 * when there is no memory. These replace RTS6x's own allocator, which carves a fixed .sysmem region the
 * C6747's linker lays out; on Windows the heap is the process's. */
#include <errno.h>
#include <stddef.h>
#include "grt.h"
#include "win32.h"

static HANDLE heap;

static HANDLE process_heap(void)
{
    if (!heap) heap = GetProcessHeap();
    return heap;
}

void *malloc(size_t size)
{
    void *p = HeapAlloc(process_heap(), 0, size ? size : 1);
    if (!p) errno = ENOMEM;
    return p;
}

void *calloc(size_t count, size_t size)
{
    void *p;
    if (size != 0 && count > (size_t)-1 / size) { errno = ENOMEM; return 0; }
    p = HeapAlloc(process_heap(), GRT_HEAP_ZERO_MEMORY, count * size ? count * size : 1);
    if (!p) errno = ENOMEM;
    return p;
}

void *realloc(void *block, size_t size)
{
    void *p;
    if (!block) return malloc(size);
    if (size == 0) { HeapFree(process_heap(), 0, block); return 0; }
    p = HeapReAlloc(process_heap(), 0, block, size);
    if (!p) errno = ENOMEM;
    return p;
}

void free(void *block)
{
    if (block) HeapFree(process_heap(), 0, block);
}
