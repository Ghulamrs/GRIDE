/* strnicmp.c - the old name Microsoft keeps in oldnames.lib for _strnicmp (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <string.h>

int strnicmp(const char *a, const char *b, size_t n)
{
    return _strnicmp(a, b, n);
}
