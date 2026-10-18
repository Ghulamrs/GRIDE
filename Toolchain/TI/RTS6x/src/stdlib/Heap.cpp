// Spec: ISO C 7.20.3.1-4. malloc(0) gives a unique pointer (7.20.3/1 allows either); realloc of a
// null pointer is malloc, to 0 frees; calloc refuses a count times size that overflows size_t.

#include "Heap.h"
#include "../string/Memory.h"

namespace rts6x {

Heap::Block *Heap::free_;
bool Heap::started_;

void Heap::start()
{
    size_t base = ((size_t)__rts6x_heap + 7) & ~(size_t)7;
    size_t slack = base - (size_t)__rts6x_heap;
    started_ = true;
    // A .sysmem smaller than its own alignment slack holds no block; the difference must not wrap.
    if ((size_t)__TI_SYSMEM_SIZE < slack) return;
    size_t size = ((size_t)__TI_SYSMEM_SIZE - slack) & ~(size_t)7;
    if (size < Smallest) return;
    free_ = reinterpret_cast<Block *>(base);
    free_->size = size;
    free_->next = 0;
}

void *Heap::allocate(size_t n)
{
    if (!started_) start();
    if (n > (size_t)-1 - Header - 7) return 0;
    size_t need = needed(n);
    for (Block **link = &free_; *link; link = &(*link)->next) {
        Block *b = *link;
        if (b->size < need) continue;
        if (b->size - need >= Smallest) {
            Block *rest = reinterpret_cast<Block *>(reinterpret_cast<char *>(b) + need);
            rest->size = b->size - need;
            rest->next = b->next;
            *link = rest;
            b->size = need;
        } else {
            *link = b->next;
        }
        return payload(b);
    }
    return 0;
}

void *Heap::allocateZeroed(size_t count, size_t size)
{
    unsigned long long total = (unsigned long long)count * size;
    if (total > (unsigned long long)(size_t)-1) return 0;
    void *p = allocate((size_t)total);
    if (p) Memory::fill(p, 0, (size_t)total);
    return p;
}

void Heap::release(void *p)
{
    if (!p) return;
    Block *b = blockOf(p);
    Block *before = 0, *after = free_;
    while (after && after < b) { before = after; after = after->next; }
    b->next = after;
    if (after && reinterpret_cast<char *>(b) + b->size == reinterpret_cast<char *>(after)) {
        b->size += after->size;
        b->next = after->next;
    }
    if (before && reinterpret_cast<char *>(before) + before->size == reinterpret_cast<char *>(b)) {
        before->size += b->size;
        before->next = b->next;
    } else if (before) {
        before->next = b;
    } else {
        free_ = b;
    }
}

void *Heap::resize(void *p, size_t n)
{
    if (!p) return allocate(n);
    if (n == 0) { release(p); return 0; }
    Block *b = blockOf(p);
    if (b->size >= needed(n)) return p;
    void *q = allocate(n);
    if (!q) return 0;
    Memory::copy(q, p, b->size - Header);
    release(p);
    return q;
}

}  // namespace rts6x
