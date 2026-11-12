/* strlwr.c - the old name Microsoft keeps in oldnames.lib for _strlwr (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <string.h>

char *strlwr(char *s)
{
    return _strlwr(s);
}
