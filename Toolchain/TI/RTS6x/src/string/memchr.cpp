// Spec: ISO C 7.21.5.1 - memchr.

#include <string.h>
#include "Memory.h"

extern "C" void *memchr(const void *in, int value, size_t n)
{
    return rts6x::Memory::find(in, value, n);
}
