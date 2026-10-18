// Spec: ISO C 7.20.3.1 - calloc.

#include <stdlib.h>
#include "Heap.h"

extern "C" void *calloc(size_t count, size_t size)
{
    return rts6x::Heap::allocateZeroed(count, size);
}
