/* stricmp.c - the old name Microsoft keeps in oldnames.lib for _stricmp (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <string.h>

int stricmp(const char *a, const char *b)
{
    return _stricmp(a, b);
}
