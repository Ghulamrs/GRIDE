// Spec: ISO C++11 18.6.1.1/2 - operator new(size_t): size bytes, or std::bad_alloc.

#include "Allocation.h"

void *operator new(size_t size)
{
    return rts6x::Allocation::allocate(size);
}
