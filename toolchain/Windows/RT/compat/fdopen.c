/* fdopen.c - the old name Microsoft keeps in oldnames.lib for _fdopen (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <stdio.h>

FILE *fdopen(int fd, const char *mode)
{
    return _fdopen(fd, mode);
}
