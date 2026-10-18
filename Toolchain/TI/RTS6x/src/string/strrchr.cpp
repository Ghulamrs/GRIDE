// Spec: ISO C 7.21.5.5 - strrchr.

#include <string.h>
#include "CString.h"

extern "C" char *strrchr(const char *s, int c)
{
    return rts6x::CString::findLast(s, c);
}
