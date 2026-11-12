/* putenv.c - the old name Microsoft keeps in oldnames.lib for _putenv (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <stdlib.h>

int putenv(const char *entry)
{
    return _putenv(entry);
}