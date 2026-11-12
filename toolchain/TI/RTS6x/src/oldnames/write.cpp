// Spec: Microsoft C runtime's old name write for _write (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <io.h>

extern "C" int write(int fd, const void *buffer, unsigned int count)
{
    return _write(fd, buffer, count);
}
