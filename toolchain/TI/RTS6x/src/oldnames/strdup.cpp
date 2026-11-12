// Spec: Microsoft C runtime's old name strdup for _strdup (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <string.h>

extern "C" char *strdup(const char *s)
{
    return _strdup(s);
}
