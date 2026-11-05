// Spec: ISO C 7.21.4.3 - strcoll, which in the "C" locale compares as strcmp does.

#include <string.h>
#include "CString.h"

extern "C" int strcoll(const char *a, const char *b)
{
    return rts6x::CString::compare(a, b);
}
