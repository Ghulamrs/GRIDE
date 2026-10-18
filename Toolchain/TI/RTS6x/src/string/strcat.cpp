// Spec: ISO C 7.21.3.1 - strcat.

#include <string.h>
#include "CString.h"

extern "C" char *strcat(char *to, const char *from)
{
    return rts6x::CString::append(to, from);
}
