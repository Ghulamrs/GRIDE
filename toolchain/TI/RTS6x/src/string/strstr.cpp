// Spec: ISO C 7.21.5.7 - strstr.

#include <string.h>
#include "CString.h"

extern "C" char *strstr(const char *s, const char *text)
{
    return rts6x::CString::find(s, text);
}
