/* open.c - the old name Microsoft keeps in oldnames.lib for _open (c90), in grtcompat: the permission argument
   passed on when there is one. */
#define _POSIX_SOURCE 1
#include <fcntl.h>
#include <io.h>
#include <stdarg.h>

int open(const char *path, int oflag, ...)
{
    int permission = 0;
    va_list args;
    if (oflag & _O_CREAT) {
        va_start(args, oflag);
        permission = va_arg(args, int);
        va_end(args);
    }
    return _open(path, oflag, permission);
}