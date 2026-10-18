// Spec: ISO C 7.20.3.3 - malloc.

#include <stdlib.h>
#include "Heap.h"

extern "C" void *malloc(size_t n)
{
    return rts6x::Heap::allocate(n);
}
