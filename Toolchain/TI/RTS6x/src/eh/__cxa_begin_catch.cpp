// Spec: Itanium C++ ABI EH 2.5.3 - __cxa_begin_catch, given what the landing pad received (the
// header), answers what the handler receives: the object, its base subobject, or a pointer's place.

#include "Exception.h"

extern "C" void *__cxa_begin_catch(void *exception)
{
    return static_cast<rts6x::Exception *>(exception)->begin();
}
