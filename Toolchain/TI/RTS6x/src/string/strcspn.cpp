// Spec: ISO C 7.21.5.3 - strcspn.

#include <string.h>
#include "CString.h"

extern "C" size_t strcspn(const char *s, const char *set)
{
    return rts6x::CString::spanNot(s, set);
}
