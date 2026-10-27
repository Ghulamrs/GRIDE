/* getcwd.c - _getcwd, the working directory in the ANSI code page (c90).
 *
 * Spec: Microsoft C runtime, _getcwd - the current drive's directory made whole ("X:." through GetFullPathNameW,
 * "." where the working directory has no drive letter), in the ANSI code page. Into buf when it and its terminator
 * fit in size bytes; else a null pointer, errno ERANGE and buf[0] 0. A null buf: a block from malloc of size bytes
 * or the path's own, whichever is larger. A negative size, or size 0 with a buf, fails fast, as the runtime's check
 * of its parameters does. */
#include <errno.h>
#include <stdlib.h>
#include "grt.h"
#include "win32.h"

/* GetFullPathNameW's answer for path, or GetCurrentDirectoryW's for a null path, in memory from the process heap;
   0 with errno set */
static WCHAR *asked(const WCHAR *path)
{
    WCHAR *s = 0;
    DWORD capacity = 0, n;
    for (;;) {
        n = path ? GetFullPathNameW(path, capacity, s, 0) : GetCurrentDirectoryW(capacity, s);
        if (n == 0) { __grt_set_errno_from(GetLastError()); break; }
        if (n < capacity) return s;
        if (s) HeapFree(GetProcessHeap(), 0, s);
        capacity = n;
        s = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (unsigned long long)capacity * sizeof(WCHAR));
        if (!s) { errno = ENOMEM; return 0; }
    }
    if (s) HeapFree(GetProcessHeap(), 0, s);
    return 0;
}

int __grt_drive(void)
{
    int saved = errno, drive = 0;
    WCHAR *here = asked(0);
    errno = saved;
    if (here && here[0] && here[1] == ':')
        drive = (here[0] >= 'a' && here[0] <= 'z' ? here[0] - 'a' + 'A' : here[0]) - 'A' + 1;
    if (here) HeapFree(GetProcessHeap(), 0, here);
    return drive;
}

char *_getcwd(char *buf, int size)
{
    WCHAR where[4], *full;
    int drive, need;
    char *out = 0;
    if (size < 0) __grt_fail_fast();
    drive = __grt_drive();
    where[0] = '.';
    where[1] = 0;
    if (drive != 0) {
        where[0] = (WCHAR)('A' - 1 + drive);
        where[1] = ':';
        where[2] = '.';
        where[3] = 0;
    }
    if (buf) {
        if (size == 0) __grt_fail_fast();
        buf[0] = 0;
    }
    full = asked(where);
    if (!full) return 0;
    need = WideCharToMultiByte(GRT_CP_ACP, 0, full, -1, 0, 0, 0, 0);
    if (need <= 0) __grt_set_errno_from(GetLastError());
    else if (buf) {
        if (need > size) errno = ERANGE;
        else out = buf;
    } else
        out = (char *)malloc((size_t)(need > size ? need : size));
    if (out) WideCharToMultiByte(GRT_CP_ACP, 0, full, -1, out, need, 0, 0);
    HeapFree(GetProcessHeap(), 0, full);
    return out;
}
