// Spec: Microsoft C runtime's old name dup2 for _dup2 (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <io.h>

extern "C" int dup2(int fd, int to)
{
    return _dup2(fd, to);
}
