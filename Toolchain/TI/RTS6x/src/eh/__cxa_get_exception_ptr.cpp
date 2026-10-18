// Spec: Itanium C++ ABI EH 2.5.3 - __cxa_get_exception_ptr: what __cxa_begin_catch will answer,
// without beginning the catch, for a parameter copied first.

#include "Exception.h"

extern "C" void *__cxa_get_exception_ptr(void *exception)
{
    return static_cast<rts6x::Exception *>(exception)->adjusted;
}
