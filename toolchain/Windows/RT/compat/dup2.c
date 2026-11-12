/* dup2.c - the old name Microsoft keeps in oldnames.lib for _dup2 (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <io.h>

int dup2(int fd, int to)
{
    return _dup2(fd, to);
}
