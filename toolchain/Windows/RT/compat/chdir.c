/* chdir.c - the old name Microsoft keeps in oldnames.lib for _chdir (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <direct.h>

int chdir(const char *path)
{
    return _chdir(path);
}
