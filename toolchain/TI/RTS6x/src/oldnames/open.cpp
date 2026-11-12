// Spec: Microsoft C runtime's old name open for _open (oldnames.lib), here for the C6747: the permission argument
// passed on when there is one.

#define _POSIX_SOURCE 1
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>

extern "C" int open(const char *path, int oflag, ...)
{
    int permission = 0;
    if (oflag & _O_CREAT) {
        va_list args;
        va_start(args, oflag);
        permission = va_arg(args, int);
        va_end(args);
    }
    return _open(path, oflag, permission);
}