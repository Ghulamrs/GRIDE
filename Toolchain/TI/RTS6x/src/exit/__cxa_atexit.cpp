// Spec: Itanium C++ ABI 3.3.5 - __cxa_atexit registers a static object's destructor, and
// __dso_handle names the program; there is one object, so it is 0, as for any executable.

#include "Termination.h"

extern "C" {
void *__dso_handle = 0;

int __cxa_atexit(void (*destructor)(void *), void *object, void *dso)
{
    (void)dso;
    return rts6x::Termination::add(destructor, object);
}
}
