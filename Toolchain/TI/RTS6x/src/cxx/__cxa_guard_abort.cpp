// Spec: Itanium C++ ABI 3.3.2 - __cxa_guard_abort: the initialisation ended by an exception, to be
// tried again the next time control passes through the declaration.

#include "Guard.h"

extern "C" void __cxa_guard_abort(int *guard)
{
    rts6x::Guard g(guard);
    g.abandon();
}
