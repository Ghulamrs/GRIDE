// Spec: ISO C 7.21.5.8 - strtok.

#include <string.h>
#include "CString.h"

extern "C" char *strtok(char *s, const char *separators)
{
    return rts6x::CString::token(s, separators);
}
