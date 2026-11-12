// Spec: Microsoft C runtime's old name isatty for _isatty (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <io.h>

extern "C" int isatty(int fd)
{
    return _isatty(fd);
}
