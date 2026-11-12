// Spec: Microsoft C runtime's old name stricmp for _stricmp (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <string.h>

extern "C" int stricmp(const char *a, const char *b)
{
    return _stricmp(a, b);
}
