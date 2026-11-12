/* write.c - the old name Microsoft keeps in oldnames.lib for _write (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <io.h>

int write(int fd, const void *buffer, unsigned int count)
{
    return _write(fd, buffer, count);
}
