// Spec: Microsoft C runtime's old name read for _read (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <io.h>

extern "C" int read(int fd, void *buffer, unsigned int count)
{
    return _read(fd, buffer, count);
}
