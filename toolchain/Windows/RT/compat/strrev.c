/* strrev.c - the old name Microsoft keeps in oldnames.lib for _strrev (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <string.h>

char *strrev(char *s)
{
    return _strrev(s);
}
