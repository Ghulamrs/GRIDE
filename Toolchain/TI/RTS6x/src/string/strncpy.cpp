// Spec: ISO C 7.21.2.4 - strncpy.

#include <string.h>
#include "CString.h"

extern "C" char *strncpy(char *to, const char *from, size_t n)
{
    return rts6x::CString::copy(to, from, n);
}
