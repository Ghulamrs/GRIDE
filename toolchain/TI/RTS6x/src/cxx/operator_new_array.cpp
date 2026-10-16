// Spec: ISO C++11 18.6.1.2/2 - operator new[](size_t) is operator new(size).

#include "Allocation.h"

void *operator new[](size_t size)
{
    return rts6x::Allocation::allocate(size);
}
