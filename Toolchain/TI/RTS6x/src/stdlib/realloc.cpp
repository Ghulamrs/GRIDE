// Spec: ISO C 7.20.3.4 - realloc.

#include <stdlib.h>
#include "Heap.h"

extern "C" void *realloc(void *p, size_t n)
{
    return rts6x::Heap::resize(p, n);
}
