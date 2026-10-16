// Spec: Itanium C++ ABI 3.3.2 - __cxa_guard_release: the object is built.

#include "Guard.h"

extern "C" void __cxa_guard_release(int *guard)
{
    rts6x::Guard g(guard);
    g.release();
}
