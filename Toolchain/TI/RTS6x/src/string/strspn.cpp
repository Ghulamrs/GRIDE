// Spec: ISO C 7.21.5.6 - strspn.

#include <string.h>
#include "CString.h"

extern "C" size_t strspn(const char *s, const char *set)
{
    return rts6x::CString::span(s, set);
}
