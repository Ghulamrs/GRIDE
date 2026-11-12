/* close.c - the old name Microsoft keeps in oldnames.lib for _close (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <io.h>

int close(int fd)
{
    return _close(fd);
}
