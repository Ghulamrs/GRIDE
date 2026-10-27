/* findfile.c - _findfirst64i32, _findnext64i32 and _findclose: a directory read through Windows' find handles
 * (c90).
 *
 * Spec: Microsoft C runtime - the handle is Windows' own (FindFirstFileExW: standard information, names matched).
 * Each entry as _finddata64i32_t lays it out: Windows' attributes, FILE_ATTRIBUTE_NORMAL alone given as 0; the
 * times through local time and back (__grt_file_time), a zero time -1; the size's low 32 bits; the name in the
 * ANSI code page. A failure answers -1 with errno ENOENT (no more files, no such file or path), ENOMEM, or EINVAL
 * for any other reason, and leaves the entry as it was. A null pattern or entry, a handle 0 or -1 given to
 * _findnext64i32, and a name longer than the entry's 260 bytes fail fast, as the runtime's checks do. */
#include <errno.h>
#include "grt.h"
#include "win32.h"

/* cpp11's <io.h>: 296 bytes */
struct _finddata64i32_t {
    unsigned attrib;
    long long time_create, time_access, time_write;
    unsigned long size;
    char name[260];
};

static void failed(DWORD code)
{
    if (code == GRT_ERROR_NO_MORE_FILES || code == GRT_ERROR_FILE_NOT_FOUND || code == GRT_ERROR_PATH_NOT_FOUND)
        errno = ENOENT;
    else if (code == GRT_ERROR_NOT_ENOUGH_MEMORY) errno = ENOMEM;
    else errno = EINVAL;
}

static long long found_time(const GRT_FILETIME *t)
{
    if (t->low == 0 && t->high == 0) return -1;
    return __grt_file_time(t->low, t->high);
}

/* what Windows found, into found; 0 with errno set when its name has no form in the ANSI code page */
static int entry(const GRT_FIND_DATA *w, struct _finddata64i32_t *found)
{
    long long created = found_time(&w->ftCreationTime), accessed = found_time(&w->ftLastAccessTime);
    long long written = found_time(&w->ftLastWriteTime);
    int n = WideCharToMultiByte(GRT_CP_ACP, 0, w->cFileName, -1, 0, 0, 0, 0);
    if (n <= 0) { __grt_set_errno_from(GetLastError()); return 0; }
    if (n > (int)sizeof found->name) __grt_fail_fast();
    WideCharToMultiByte(GRT_CP_ACP, 0, w->cFileName, -1, found->name, n, 0, 0);
    found->attrib = w->dwFileAttributes == GRT_FILE_ATTRIBUTE_NORMAL ? 0u : (unsigned)w->dwFileAttributes;
    found->time_create = created;
    found->time_access = accessed;
    found->time_write = written;
    found->size = w->nFileSizeLow;
    return 1;
}

long long _findfirst64i32(const char *pattern, struct _finddata64i32_t *found)
{
    WCHAR *wide;
    GRT_FIND_DATA w;
    HANDLE h;
    if (!found || !pattern) __grt_fail_fast();
    wide = __grt_widen(pattern);
    if (!wide) return -1;
    h = FindFirstFileExW(wide, GRT_FIND_EX_INFO_STANDARD, &w, GRT_FIND_EX_SEARCH_NAME_MATCH, 0, 0);
    if (h == GRT_INVALID_HANDLE) failed(GetLastError());
    HeapFree(GetProcessHeap(), 0, wide);
    if (h == GRT_INVALID_HANDLE) return -1;
    if (!entry(&w, found)) { FindClose(h); return -1; }
    return (long long)h;
}

int _findnext64i32(long long handle, struct _finddata64i32_t *found)
{
    GRT_FIND_DATA w;
    if (handle == 0 || handle == -1 || !found) __grt_fail_fast();
    if (!FindNextFileW((HANDLE)handle, &w)) { failed(GetLastError()); return -1; }
    return entry(&w, found) ? 0 : -1;
}

int _findclose(long long handle)
{
    if (!FindClose((HANDLE)handle)) { errno = EINVAL; return -1; }
    return 0;
}
