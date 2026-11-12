/* lseek.c - the old name Microsoft keeps in oldnames.lib for _lseek (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <io.h>

long lseek(int fd, long offset, int origin)
{
    return _lseek(fd, offset, origin);
}
