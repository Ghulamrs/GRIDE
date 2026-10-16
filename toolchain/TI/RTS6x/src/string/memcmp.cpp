// Spec: ISO C 7.21.4.1 - memcmp.

#include <string.h>
#include "Memory.h"

extern "C" int memcmp(const void *a, const void *b, size_t n)
{
    return rts6x::Memory::compare(a, b, n);
}
