// Spec: Microsoft C runtime's old name dup for _dup (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <io.h>

extern "C" int dup(int fd)
{
    return _dup(fd);
}
