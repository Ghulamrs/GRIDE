/* read.c - the old name Microsoft keeps in oldnames.lib for _read (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <io.h>

int read(int fd, void *buffer, unsigned int count)
{
    return _read(fd, buffer, count);
}
