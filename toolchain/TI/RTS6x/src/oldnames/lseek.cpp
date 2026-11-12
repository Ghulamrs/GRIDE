// Spec: Microsoft C runtime's old name lseek for _lseek (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <io.h>

extern "C" long lseek(int fd, long offset, int origin)
{
    return _lseek(fd, offset, origin);
}
