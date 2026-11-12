// Spec: Microsoft C runtime's old name strlwr for _strlwr (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <string.h>

extern "C" char *strlwr(char *s)
{
    return _strlwr(s);
}
