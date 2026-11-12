// Spec: Microsoft C runtime's old name strnicmp for _strnicmp (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <string.h>

extern "C" int strnicmp(const char *a, const char *b, size_t n)
{
    return _strnicmp(a, b, n);
}
