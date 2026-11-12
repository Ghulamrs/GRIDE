// Spec: Microsoft C runtime's old name strrev for _strrev (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <string.h>

extern "C" char *strrev(char *s)
{
    return _strrev(s);
}
