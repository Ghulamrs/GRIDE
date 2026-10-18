// Spec: ISO C 7.21.5.4 - strpbrk.

#include <string.h>
#include "CString.h"

extern "C" char *strpbrk(const char *s, const char *set)
{
    return rts6x::CString::findAny(s, set);
}
