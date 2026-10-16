// Spec: Itanium C++ ABI EH 2.4.1 - __cxa_allocate_exception: room for a thrown object of size bytes,
// with the runtime's header in front of it; none is terminate.

#include <stdlib.h>
#include <string.h>
#include "Exception.h"
#include "Handlers.h"

extern "C" void *__cxa_allocate_exception(size_t size)
{
    void *p = malloc(sizeof(rts6x::Exception) + size);
    if (!p) rts6x::Handlers::terminate();
    memset(p, 0, sizeof(rts6x::Exception));
    return static_cast<rts6x::Exception *>(p)->object();
}
