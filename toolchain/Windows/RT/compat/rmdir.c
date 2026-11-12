/* rmdir.c - the old name Microsoft keeps in oldnames.lib for _rmdir (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <direct.h>

int rmdir(const char *path)
{
    return _rmdir(path);
}
