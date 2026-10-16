// Spec: ISO C 7.20.3.2 - free.

#include <stdlib.h>
#include "Heap.h"

extern "C" void free(void *p)
{
    rts6x::Heap::release(p);
}
