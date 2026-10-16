// Spec: Itanium C++ ABI 3.3.2 - __cxa_guard_acquire.

#include "Guard.h"

extern "C" int __cxa_guard_acquire(int *guard)
{
    rts6x::Guard g(guard);
    return g.acquire();
}
