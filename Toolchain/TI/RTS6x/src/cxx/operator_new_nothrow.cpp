// Spec: ISO C++11 18.6.1.1/5 - operator new(size_t, nothrow_t): the storage, or a null pointer.

#include "Allocation.h"

void *operator new(size_t size, const std::nothrow_t &) throw()
{
    return rts6x::Allocation::tryAllocate(size);
}
