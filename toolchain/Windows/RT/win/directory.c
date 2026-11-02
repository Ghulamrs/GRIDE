/* directory.c - _chdir, _mkdir and _rmdir: the working directory changed, a directory made or removed (c90).
 *
 * Spec: Microsoft C runtime - each answers 0, or -1 with errno set from Windows' error. _chdir also sets the
 * environment's "=X:" entry for the new directory's drive (none for a \\server\share path), as Windows' own shell
 * keeps it, so a path like "X:file" later means the right folder; a null path fails fast. _mkdir and _rmdir hand
 * a null path to Windows, which refuses it (ENOENT), as the runtime does. */
#include <errno.h>
#include "grt.h"
#include "win32.h"

int _chdir(const char *path)
{
    WCHAR *wide, *here = 0, name[4];
    DWORD capacity = 0, n, error = 0;
    int ok;
    if (!path) __grt_fail_fast();
    wide = __grt_widen(path);
    if (!wide) return -1;
    ok = SetCurrentDirectoryW(wide);
    if (!ok) error = GetLastError();
    HeapFree(GetProcessHeap(), 0, wide);
    if (!ok) { __grt_set_errno_from(error); return -1; }
    for (;;) {
        n = GetCurrentDirectoryW(capacity, here);
        if (n == 0) { __grt_set_errno_from(GetLastError()); break; }
        if (n < capacity) break;
        if (here) HeapFree(GetProcessHeap(), 0, here);
        capacity = n;
        here = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (unsigned long long)capacity * sizeof(WCHAR));
        if (!here) { errno = ENOMEM; return -1; }
    }
    if (n == 0) {
        if (here) HeapFree(GetProcessHeap(), 0, here);
        return -1;
    }
    ok = 1;
    if (!((here[0] == '\\' || here[0] == '/') && here[0] == here[1])) {
        name[0] = '=';
        name[1] = (WCHAR)(here[0] >= 'a' && here[0] <= 'z' ? here[0] - 'a' + 'A' : here[0]);
        name[2] = ':';
        name[3] = 0;
        ok = SetEnvironmentVariableW(name, here);
        if (!ok) __grt_set_errno_from(GetLastError());
    }
    HeapFree(GetProcessHeap(), 0, here);
    return ok ? 0 : -1;
}

int _mkdir(const char *path)
{
    WCHAR *wide = 0;
    DWORD error = 0;
    int ok;
    if (path) {
        wide = __grt_widen(path);
        if (!wide) return -1;
    }
    ok = CreateDirectoryW(wide, 0);
    if (!ok) error = GetLastError();
    if (wide) HeapFree(GetProcessHeap(), 0, wide);
    if (!ok) { __grt_set_errno_from(error); return -1; }
    return 0;
}

int _rmdir(const char *path)
{
    WCHAR *wide = 0;
    DWORD error = 0;
    int ok;
    if (path) {
        wide = __grt_widen(path);
        if (!wide) return -1;
    }
    ok = RemoveDirectoryW(wide);
    if (!ok) error = GetLastError();
    if (wide) HeapFree(GetProcessHeap(), 0, wide);
    if (!ok) { __grt_set_errno_from(error); return -1; }
    return 0;
}
