/* heap.c - malloc, calloc, realloc and free (c90).
 *
 * Spec: ISO C 7.20.3 and the Microsoft runtime's choices where C leaves one: malloc(0) a unique block
 * rather than a null pointer, realloc(p, 0) the block freed and a null pointer answered, errno ENOMEM
 * when there is no memory. These replace RTS6x's own allocator, which carves a fixed .sysmem region the
 * C6747's linker lays out.
 *
 * The Release allocator is docs\Runtime_Design.md 10.2's. A block of up to 4 KB is one of 28 size classes and
 * comes from a region reserved with VirtualAlloc and committed in 64 KB slabs, each slab carved into one class's
 * blocks; a side table names each slab's class, so a block carries no header. Each class keeps a free list and
 * the slab it is carving, under a lock of its own once the program has a second thread (9.6). A larger block,
 * a small one once 16 regions are full, and so any pointer outside the regions, is the process heap's.
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

/* win\fill.asm: rep stosb, where a byte loop compiled at -O0 took a 1 GB block twelve seconds to fill */
void __grt_fill(void *p, int value, size_t n);
#define grt_heap_fill(p, value, n) __grt_fill((p), (value), (n))

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

#define SMALL 4096
#define CLASSES 28
#define SLAB_SHIFT 16
#define SLAB ((unsigned long long)1 << SLAB_SHIFT)
#define REGION_SLABS 4096                       /* 256 MB of address space */
#define REGION ((unsigned long long)REGION_SLABS << SLAB_SHIFT)
#define REGIONS 16

struct grt_block { struct grt_block *next; };

struct grt_class {
    struct grt_block *free;
    unsigned char *next, *end;                  /* what is left of the slab being carved */
    void *lock;
};

/* a size's class by (size + 15) / 16: 16-byte steps to 128 bytes, then four to each power of two up to 4096 */
static const unsigned char classOf[257] = {
    0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15,
    15, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19,
    19, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
    21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23,
    23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25,
    25, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
    26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27,
    27
};
static const unsigned short classSize[CLASSES] = {
    16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512,
    640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2560, 3072, 3584, 4096
};

static struct grt_class classes[CLASSES];
/* free reads regions and regionCount unlocked: a region is stored before the count that shows it */
static unsigned char *regions[REGIONS];
static unsigned regionCount;
static unsigned slabsUsed;                      /* of the last region */
static unsigned char slabClass[REGIONS][REGION_SLABS];
static void *slabLock;

/* a new slab for class c to carve: 0 when no region can be reserved or committed */
static int grt_heap_slab(unsigned c, int locked)
{
    unsigned char *slab = 0;
    if (locked) __grt_lock(&slabLock);
    if ((regionCount == 0 || slabsUsed == REGION_SLABS) && regionCount < REGIONS) {
        unsigned char *r = (unsigned char *)VirtualAlloc(0, REGION, GRT_MEM_RESERVE, GRT_PAGE_READWRITE);
        if (r) {
            regions[regionCount] = r;
            regionCount++;
            slabsUsed = 0;
        }
    }
    if (regionCount && slabsUsed < REGION_SLABS) {
        slab = regions[regionCount - 1] + ((unsigned long long)slabsUsed << SLAB_SHIFT);
        if (VirtualAlloc(slab, SLAB, GRT_MEM_COMMIT, GRT_PAGE_READWRITE))
            slabClass[regionCount - 1][slabsUsed++] = (unsigned char)c;
        else
            slab = 0;
    }
    if (locked) __grt_unlock(&slabLock);
    if (!slab) return 0;
    classes[c].next = slab;
    classes[c].end = slab + SLAB;
    return 1;
}

/* a block's class, or -1 for the process heap's */
static int grt_heap_class(const void *block)
{
    unsigned r;
    for (r = 0; r < regionCount; r++) {
        unsigned long long at = (unsigned long long)block - (unsigned long long)regions[r];
        if (at < REGION) return slabClass[r][at >> SLAB_SHIFT];
    }
    return -1;
}

void *malloc(size_t size)
{
    struct grt_class *k;
    struct grt_block *b;
    unsigned c;
    int locked;
    if (size > SMALL) {
        b = (struct grt_block *)HeapAlloc(grt_process_heap(), 0, size);
        if (!b) errno = ENOMEM;
        return b;
    }
    c = classOf[(size + 15) >> 4];
    k = &classes[c];
    locked = __grt_threads;
    if (locked) __grt_lock(&k->lock);
    b = k->free;
    if (b)
        k->free = b->next;
    else if ((unsigned long long)(k->end - k->next) >= classSize[c] || grt_heap_slab(c, locked)) {
        b = (struct grt_block *)k->next;
        k->next += classSize[c];
    }
    if (locked) __grt_unlock(&k->lock);
    if (!b) {
        b = (struct grt_block *)HeapAlloc(grt_process_heap(), 0, classSize[c]);
        if (!b) errno = ENOMEM;
    }
    return b;
}

void free(void *block)
{
    struct grt_class *k;
    int c, locked;
    if (!block) return;
    c = grt_heap_class(block);
    if (c < 0) { HeapFree(grt_process_heap(), 0, block); return; }
    k = &classes[c];
    locked = __grt_threads;
    if (locked) __grt_lock(&k->lock);
    ((struct grt_block *)block)->next = k->free;
    k->free = (struct grt_block *)block;
    if (locked) __grt_unlock(&k->lock);
}

void *calloc(size_t count, size_t size)
{
    unsigned long long *p;
    size_t n, i;
    if (size != 0 && count > (size_t)-1 / size) { errno = ENOMEM; return 0; }
    n = count * size;
    if (n > SMALL) {
        p = (unsigned long long *)HeapAlloc(grt_process_heap(), GRT_HEAP_ZERO_MEMORY, n);
        if (!p) errno = ENOMEM;
        return p;
    }
    p = (unsigned long long *)malloc(n);
    if (p)
        for (i = 0; i < (n + 7) >> 3; i++) p[i] = 0;
    return p;
}

/* a block grows in place within its class and moves otherwise; the process heap's are its to resize */
void *realloc(void *block, size_t size)
{
    unsigned long long *p;
    const unsigned long long *q;
    size_t i;
    int c;
    if (!block) return malloc(size);
    if (size == 0) { free(block); return 0; }
    c = grt_heap_class(block);
    if (c < 0) {
        p = (unsigned long long *)HeapReAlloc(grt_process_heap(), 0, block, size);
        if (!p) errno = ENOMEM;
        return p;
    }
    if (size <= classSize[c]) return block;
    p = (unsigned long long *)malloc(size);
    if (!p) return 0;
    q = (const unsigned long long *)block;
    for (i = 0; i < (size_t)(classSize[c] >> 3); i++) p[i] = q[i];
    free(block);
    return p;
}

#endif
