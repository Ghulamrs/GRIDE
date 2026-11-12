/* getpid.c - the old name Microsoft keeps in oldnames.lib for _getpid (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <process.h>

int getpid(void)
{
    return _getpid();
}