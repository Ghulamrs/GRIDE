/* dup.c - the old name Microsoft keeps in oldnames.lib for _dup (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <io.h>

int dup(int fd)
{
    return _dup(fd);
}
