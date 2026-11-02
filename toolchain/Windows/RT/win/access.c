/* access.c - _access, whether a path names something, and whether it can be written (c90).
 *
 * Spec: Microsoft C runtime, _access - mode 0 asks whether the path names something, 2 whether it can be written,
 * 4 whether read, 6 both: 0 when so, else -1 with errno set from Windows' error (ENOENT for no such path). A
 * directory answers 0 to every mode; a read-only file -1 and EACCES to a mode with 2. A null path or a mode with
 * other bits fails fast, as the runtime's check of its parameters does. */
#include <errno.h>
#include "grt.h"
#include "win32.h"

int _access(const char *path, int mode)
{
    WCHAR *wide;
    GRT_FILE_ATTRIBUTE_DATA data;
    DWORD error = 0;
    int ok;
    if (!path || (mode & ~6) != 0) __grt_fail_fast();
    wide = __grt_widen(path);
    if (!wide) return -1;
    ok = GetFileAttributesExW(wide, 0, &data);
    if (!ok) error = GetLastError();
    HeapFree(GetProcessHeap(), 0, wide);
    if (!ok) { __grt_set_errno_from(error); return -1; }
    if (data.dwFileAttributes & GRT_FILE_ATTRIBUTE_DIRECTORY) return 0;
    if ((data.dwFileAttributes & GRT_FILE_ATTRIBUTE_READONLY) && (mode & 2)) { errno = EACCES; return -1; }
    return 0;
}
