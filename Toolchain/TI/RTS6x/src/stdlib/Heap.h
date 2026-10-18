// Spec: ISO C 7.20.3 - malloc, calloc, realloc and free over .sysmem: storage aligned for any
// object (8 bytes on the C6000), null when there is none, a freed block available again.
// First fit over a free list in address order, so that freeing merges a block with its neighbours.
#ifndef RTS6X_HEAP_H
#define RTS6X_HEAP_H

#include <stddef.h>

extern "C" {
extern char __rts6x_heap[];
extern char __TI_SYSMEM_SIZE[];     // an absolute symbol: its address is the heap's size
}

namespace rts6x {

class Heap {
public:
    static void *allocate(size_t n);
    static void *allocateZeroed(size_t count, size_t size);
    static void *resize(void *p, size_t n);
    static void release(void *p);

private:
    // Each block starts with this: its whole size, header included, a multiple of 8; and, while it
    // is free, the next free block.
    struct Block {
        size_t size;
        Block *next;
    };
    enum { Header = 8, Smallest = 16 };

    static void start();
    static size_t needed(size_t n) { size_t m = (n + Header + 7) & ~(size_t)7; return m < Smallest ? Smallest : m; }
    static Block *blockOf(void *p) { return reinterpret_cast<Block *>(static_cast<char *>(p) - Header); }
    static void *payload(Block *b) { return reinterpret_cast<char *>(b) + Header; }

    static Block *free_;
    static bool started_;
};

}  // namespace rts6x

#endif
