/* stat.c - _stat64i32, a file's kind, size and times by its path (c90).
 *
 * Spec: Microsoft C runtime, _stat64i32, into struct _stat64i32 as cpp11's <sys/stat.h> lays it out:
 * - The path is opened (CreateFileW, attributes only, a directory too): a disk file is read with
 *   GetFileInformationByHandle, st_nlink 1 however many links it has; a character device is _S_IFCHR and a pipe
 *   _S_IFIFO, st_dev and st_rdev -1, a pipe's size what PeekNamedPipe says is waiting. (The UCRT's sources also
 *   have a GetFileInformationByName path that reports the link count; the shipped library does not take it -
 *   tests\filing's link1.txt.)
 * - A path that cannot be opened is still a directory when it names a drive's root or a share's (\\server\share)
 *   that Windows knows, its times 1980-01-01 00:00 local; any other is ENOENT.
 * - st_mode: _S_IFDIR (a directory, a root, "X:" with nothing after it) or _S_IFREG; read always, write unless
 *   read-only, execute for a directory or a name ending .exe, .cmd, .bat or .com; the three repeated for the group
 *   and others. st_dev and st_rdev: the drive the path names, else the current one (A 0, B 1, ...); "X:" alone is
 *   ENOENT.
 * - Times through local time and back (__grt_file_time): a zero write time 0, a zero access or creation time the
 *   write time. A size past 2^31-1 is EOVERFLOW.
 * On failure the struct is zeroed and -1 answered. A null path or struct fails fast, as the runtime's check of its
 * parameters does. */
#include <errno.h>
#include "grt.h"
#include "win32.h"

#define EOVERFLOW 132
#define S_IFDIR  0x4000
#define S_IFCHR  0x2000
#define S_IFIFO  0x1000
#define S_IFREG  0x8000
#define S_IREAD  0x0100
#define S_IWRITE 0x0080
#define S_IEXEC  0x0040

/* cpp11's <sys/stat.h>: 48 bytes */
struct _stat64i32 {
    unsigned st_dev;
    unsigned short st_ino, st_mode;
    short st_nlink, st_uid, st_gid;
    unsigned st_rdev;
    long st_size;
    long long st_atime, st_mtime, st_ctime;
};

static int slash(WCHAR c) { return c == '\\' || c == '/'; }
static int letter(WCHAR c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

static int root_or_empty(const WCHAR *path)
{
    if (letter(path[0]) && path[1] == ':') path += 2;
    return path[0] == 0 || (slash(path[0]) && path[1] == 0);
}

static int executable(const WCHAR *path)
{
    static const char kinds[4][5] = { ".exe", ".cmd", ".bat", ".com" };
    const WCHAR *dot = 0;
    int k, i;
    for (; *path; path++) if (*path == '.') dot = path;
    if (!dot) return 0;
    for (k = 0; k < 4; k++) {
        for (i = 0; kinds[k][i]; i++) {
            WCHAR c = dot[i];
            if (c >= 'A' && c <= 'Z') c = (WCHAR)(c - 'A' + 'a');
            if (c != (WCHAR)kinds[k][i]) break;
        }
        if (!kinds[k][i] && !dot[i]) return 1;
    }
    return 0;
}

static unsigned short mode_of(DWORD attributes, const WCHAR *path)
{
    unsigned short m;
    attributes &= 0xFF;
    m = (unsigned short)((attributes & GRT_FILE_ATTRIBUTE_DIRECTORY) || root_or_empty(path) ? S_IFDIR | S_IEXEC : S_IFREG);
    m |= (attributes & GRT_FILE_ATTRIBUTE_READONLY) ? S_IREAD : S_IREAD | S_IWRITE;
    if (executable(path)) m |= S_IEXEC;
    m |= (m & 0700) >> 3;
    m |= (m & 0700) >> 6;
    return m;
}

static int drive_of(const WCHAR *path, struct _stat64i32 *s)
{
    int drive;
    if (letter(path[0]) && path[1] == ':') {
        if (path[2] == 0) { __grt_set_errno_from(GRT_ERROR_FILE_NOT_FOUND); return 0; }
        drive = (path[0] >= 'A' && path[0] <= 'Z' ? path[0] - 'A' + 'a' : path[0]) - 'a' + 1;
    } else
        drive = __grt_drive();
    s->st_dev = s->st_rdev = (unsigned)(drive - 1);
    return 1;
}

static long long stat_time(const GRT_FILETIME *t, long long otherwise)
{
    if (t->low == 0 && t->high == 0) return otherwise;
    return __grt_file_time(t->low, t->high);
}

static int by_handle(const WCHAR *path, HANDLE h, struct _stat64i32 *s)
{
    GRT_FILE_INFORMATION i;
    DWORD type = GetFileType(h) & ~(DWORD)GRT_FILE_TYPE_REMOTE, waiting;
    if (type == GRT_FILE_TYPE_CHAR || type == GRT_FILE_TYPE_PIPE) {
        s->st_mode = (unsigned short)(type == GRT_FILE_TYPE_CHAR ? S_IFCHR : S_IFIFO);
        s->st_nlink = 1;
        s->st_dev = s->st_rdev = (unsigned)-1;
        if (type == GRT_FILE_TYPE_PIPE && PeekNamedPipe(h, 0, 0, 0, &waiting, 0)) s->st_size = (long)waiting;
        return 1;
    }
    if (type == GRT_FILE_TYPE_UNKNOWN) { errno = EBADF; return 0; }
    if (type != GRT_FILE_TYPE_DISK) { __grt_set_errno_from(GetLastError()); return 0; }
    s->st_nlink = 1;
    if (!drive_of(path, s)) return 0;
    if (!GetFileInformationByHandle(h, &i)) { __grt_set_errno_from(GetLastError()); return 0; }
    s->st_mode = mode_of(i.dwFileAttributes, path);
    s->st_mtime = stat_time(&i.ftLastWriteTime, 0);
    s->st_atime = stat_time(&i.ftLastAccessTime, s->st_mtime);
    s->st_ctime = stat_time(&i.ftCreationTime, s->st_mtime);
    if (i.nFileSizeHigh != 0 || i.nFileSizeLow > 2147483647ul) { errno = EOVERFLOW; return 0; }
    s->st_size = (long)i.nFileSizeLow;
    return 1;
}

/* \\server\share, with or without one slash after it */
static int share_root(const WCHAR *path)
{
    const WCHAR *p;
    unsigned n = 0;
    while (path[n]) n++;
    if (n < 5 || !slash(path[0]) || !slash(path[1]) || slash(path[2])) return 0;
    p = path + 2;
    while (*++p) if (slash(*p)) break;
    if (p[0] == 0 || p[1] == 0) return 0;
    while (*++p) if (slash(*p)) break;
    return p[0] == 0 || p[1] == 0;
}

/* a root Windows knows, though it could not be opened (a drive with no medium) */
static int usable_root(const WCHAR *path)
{
    WCHAR *full = 0;
    DWORD capacity = 0, n;
    const WCHAR *p;
    int root = 0;
    for (p = path; *p && *p != '.' && !slash(*p); p++) {}
    if (!*p) return 0;
    for (;;) {
        n = GetFullPathNameW(path, capacity, full, 0);
        if (n == 0) break;
        if (n < capacity) { root = n == 3 || share_root(full); break; }
        if (full) HeapFree(GetProcessHeap(), 0, full);
        capacity = n;
        full = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (unsigned long long)capacity * sizeof(WCHAR));
        if (!full) break;
    }
    if (full) HeapFree(GetProcessHeap(), 0, full);
    return root && GetDriveTypeW(path) > GRT_DRIVE_NO_ROOT_DIR;
}

static int unopened(const WCHAR *path, struct _stat64i32 *s)
{
    int fields[9];
    if (!usable_root(path)) { __grt_set_errno_from(GRT_ERROR_FILE_NOT_FOUND); return 0; }
    s->st_mode = mode_of(GRT_FILE_ATTRIBUTE_DIRECTORY, path);
    s->st_nlink = 1;
    if (!drive_of(path, s)) return 0;
    fields[0] = fields[1] = fields[2] = 0;
    fields[3] = 1;
    fields[4] = 0;
    fields[5] = 80;
    fields[6] = fields[7] = 0;
    fields[8] = -1;
    s->st_mtime = s->st_atime = s->st_ctime = __grt_from_local(fields, -1);
    return 1;
}

static void clear(struct _stat64i32 *s)
{
    s->st_dev = s->st_rdev = 0;
    s->st_ino = s->st_mode = 0;
    s->st_nlink = s->st_uid = s->st_gid = 0;
    s->st_size = 0;
    s->st_atime = s->st_mtime = s->st_ctime = 0;
}

int _stat64i32(const char *path, struct _stat64i32 *buf)
{
    WCHAR *wide;
    HANDLE h;
    int ok;
    if (!path) __grt_fail_fast();
    wide = __grt_widen(path);
    if (!wide) return -1;
    if (!buf) __grt_fail_fast();
    clear(buf);
    h = CreateFileW(wide, GRT_FILE_READ_ATTRIBUTES, GRT_SHARE_ALL, 0, GRT_OPEN_EXISTING, GRT_FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (h != GRT_INVALID_HANDLE) {
        ok = by_handle(wide, h, buf);
        CloseHandle(h);
    } else
        ok = unopened(wide, buf);
    if (!ok) clear(buf);
    HeapFree(GetProcessHeap(), 0, wide);
    return ok ? 0 : -1;
}
