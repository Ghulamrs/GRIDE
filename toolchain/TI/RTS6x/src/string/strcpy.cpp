// Spec: ISO C 7.21.2.3 - strcpy.

#include <string.h>
#include "CString.h"

extern "C" char *strcpy(char *to, const char *from)
{
    return rts6x::CString::copy(to, from);
}
