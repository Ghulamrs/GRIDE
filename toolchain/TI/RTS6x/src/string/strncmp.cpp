// Spec: ISO C 7.21.4.4 - strncmp.

#include <string.h>
#include "CString.h"

extern "C" int strncmp(const char *a, const char *b, size_t n)
{
    return rts6x::CString::compare(a, b, n);
}
