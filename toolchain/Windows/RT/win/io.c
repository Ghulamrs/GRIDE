/* io.c - Microsoft's descriptor calls (c90): _open, _close, _read, _write, _lseek and _unlink over the runtime's own
 * descriptors (files.c), as the UCRT has them (docs\Runtime_Design.md 9.4). _open's _O_ flags are Microsoft's and
 * go to files.c's open in the numbering it shares with RTS6x's streams (the C$$IO$$ host's); _O_CREAT's _S_IWRITE
 * is not looked at: a file is made writable. A null path or buffer fails fast, as the UCRT's invalid parameter
 * does; a count past INT_MAX is EINVAL. */
#include <errno.h>
#include <stdarg.h>
#include "grt.h"
#include "win32.h"

#define MS_O_APPEND    0x0008
#define MS_O_TEMPORARY 0x0040
#define MS_O_CREAT     0x0100
#define MS_O_TRUNC     0x0200
#define MS_O_EXCL      0x0400
#define MS_O_BINARY    0x8000

int _open(const char *path, int oflag, ...)
{
    unsigned flags = (unsigned)oflag & 3u;
    if (!path) __grt_fail_fast();
    if ((oflag & 3) == 3) { errno = EINVAL; return -1; }
    if (oflag & MS_O_APPEND) flags |= 0x008;
    if (oflag & MS_O_TEMPORARY) flags |= 0x040;
    if (oflag & MS_O_CREAT) flags |= 0x200;
    if (oflag & MS_O_TRUNC) flags |= 0x400;
    if ((oflag & MS_O_EXCL) && (oflag & MS_O_CREAT)) flags |= 0x100;
    if (oflag & MS_O_BINARY) flags |= 0x8000;
    return __grt_open(path, flags);
}

int _close(int fd)
{
    return __grt_close(fd);
}

int _read(int fd, void *buffer, unsigned int count)
{
    if (count > 2147483647u) { errno = EINVAL; return -1; }
    if (!buffer && count != 0) __grt_fail_fast();
    return __grt_read(fd, (char *)buffer, count);
}

int _write(int fd, const void *buffer, unsigned int count)
{
    if (!buffer && count != 0) __grt_fail_fast();
    return __grt_write(fd, (const char *)buffer, count);
}

long _lseek(int fd, long offset, int origin)
{
    return __grt_seek(fd, offset, origin);
}

int _unlink(const char *path)
{
    if (!path) __grt_fail_fast();
    return __grt_unlink(path);
}