// Spec: Microsoft C runtime's old name close for _close (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <io.h>

extern "C" int close(int fd)
{
    return _close(fd);
}
