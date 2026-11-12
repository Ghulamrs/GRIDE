// Spec: Microsoft C runtime's old name fdopen for _fdopen (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <stdio.h>

extern "C" FILE *fdopen(int fd, const char *mode)
{
    return _fdopen(fd, mode);
}
