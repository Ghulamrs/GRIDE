/* unlink.c - the old name Microsoft keeps in oldnames.lib for _unlink (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <io.h>

int unlink(const char *path)
{
    return _unlink(path);
}
