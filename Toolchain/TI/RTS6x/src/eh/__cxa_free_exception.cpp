// Spec: Itanium C++ ABI EH 2.4.2 - __cxa_free_exception: the room back, the object never thrown.

#include <stdlib.h>
#include "Exception.h"

extern "C" void __cxa_free_exception(void *object)
{
    free(rts6x::Exception::of(object));
}
