/* strdup.c - the old name Microsoft keeps in oldnames.lib for _strdup (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <string.h>

char *strdup(const char *s)
{
    return _strdup(s);
}
