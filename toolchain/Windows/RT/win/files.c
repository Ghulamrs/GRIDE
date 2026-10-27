/* files.c - the descriptors, over Windows' handles (c90).
 *
 * Spec: Microsoft C runtime - _write and _read in text mode (LF written as CR LF; CR LF read as LF, a CR that
 * ends a read looked past to see whether an LF follows, CTRL+Z the end of a file or a pipe but a character
 * on a device), _open's flags, _setmode, _isatty, 8,192 descriptors, and the errno a Windows error number
 * maps to (the runtime's errno table).
 *
 * The streams (c\Stream*.cpp) read and write raw bytes here and translate in their own buffers; _read-style
 * reads of a handle that cannot seek go through a buffer of the descriptor's own, so that a CR at the end of
 * a read can be looked past. The table grows in blocks of 64 descriptors as files are opened. Nothing here
 * calls the C library: this is the lowest layer, and kernel32 is all it calls.
 *
 * Locks, as the Microsoft runtime's lowio takes them, once the program has threads (__grt_threads): each read,
 * write, seek, close and _setmode holds its descriptor's lock, and a free descriptor is claimed under the table's,
 * marked CLAIMED until its file is open, so that two opens never take one descriptor. A close frees it by its last
 * store, mode 0. A block, once allocated, is never freed, so a descriptor is found without the table's lock. */
#include <errno.h>
#include <stddef.h>
#include "grt.h"
#include "win32.h"

#define AHEAD 4096
#define CTRL_Z 0x1A
#define BLOCK 64
#define BLOCKS (GRT_DESCRIPTORS / BLOCK)
#define CLAIMED 1               /* a mode: taken by an open still under way, neither open nor free */

struct Descriptor {
    HANDLE handle;
    int mode;               /* GRT_TEXT or GRT_BINARY; 0 when the descriptor is not open, or CLAIMED */
    int kind;               /* GRT_KIND_ */
    int append;             /* every write at the end (fopen's "a") */
    int ended;              /* a text read met CTRL+Z */
    unsigned char *ahead;   /* a handle that cannot seek: bytes read from Windows, not yet given */
    unsigned aheadAt, aheadEnd;
    void *lock;             /* an SRWLOCK, held for each call on the descriptor once there are threads */
};

static struct Descriptor first[BLOCK];
static struct Descriptor *blocks[BLOCKS] = { first };
static void *table;         /* an SRWLOCK: the growing of blocks and the claiming of a free descriptor */

/* the Microsoft runtime's errno for a Windows error number */
void __grt_set_errno_from(unsigned long code)
{
    static const unsigned short map[][2] = {
        { 1, EINVAL }, { 2, ENOENT }, { 3, ENOENT }, { 4, 24 }, { 5, EACCES }, { 6, EBADF }, { 7, ENOMEM },
        { 8, ENOMEM }, { 9, ENOMEM }, { 10, 7 }, { 11, 8 }, { 12, EINVAL }, { 13, EINVAL }, { 15, ENOENT },
        { 16, EACCES }, { 17, 18 }, { 18, ENOENT }, { 33, EACCES }, { 53, ENOENT }, { 65, EACCES },
        { 67, ENOENT }, { 80, EEXIST }, { 82, EACCES }, { 83, EACCES }, { 87, EINVAL }, { 89, 11 },
        { 108, EACCES }, { 109, 32 }, { 112, ENOSPC }, { 114, EBADF }, { 128, 10 }, { 129, 10 },
        { 130, EBADF }, { 131, EINVAL }, { 132, EACCES }, { 145, 41 }, { 158, EACCES }, { 161, ENOENT },
        { 164, 11 }, { 167, EACCES }, { 183, EEXIST }, { 206, ENOENT }, { 215, 11 }, { 1816, ENOMEM }
    };
    unsigned i;
    for (i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i][0] == code) { errno = map[i][1]; return; }
    if (code >= 19 && code <= 36) errno = EACCES;           /* write protect .. sharing buffer exceeded */
    else if (code >= 188 && code <= 202) errno = 8;         /* bad executable: ENOEXEC */
    else errno = EINVAL;
}

static struct Descriptor *slot_of(int fd)
{
    struct Descriptor *b;
    if (fd < 0 || fd >= GRT_DESCRIPTORS) return 0;
    b = blocks[fd / BLOCK];
    return b ? &b[fd % BLOCK] : 0;
}

static int is_open(const struct Descriptor *d)
{
    return d->mode == GRT_TEXT || d->mode == GRT_BINARY;
}

/* an open descriptor, unlocked: for what reads its mode and kind alone */
static struct Descriptor *open_descriptor(int fd)
{
    struct Descriptor *d = slot_of(fd);
    if (!d || !is_open(d)) { errno = EBADF; return 0; }
    return d;
}

static void give(struct Descriptor *d, int held)
{
    if (held) ReleaseSRWLockExclusive(&d->lock);
}

/* an open descriptor for one call, locked when the program has threads; *held says whether, for give().
   0 with errno EBADF when fd is not open, or was closed while the lock was awaited. */
static struct Descriptor *take(int fd, int *held)
{
    struct Descriptor *d = slot_of(fd);
    *held = 0;
    if (!d) { errno = EBADF; return 0; }
    if (__grt_threads) {
        AcquireSRWLockExclusive(&d->lock);
        *held = 1;
    }
    if (!is_open(d)) {
        give(d, *held);
        errno = EBADF;
        return 0;
    }
    return d;
}

static int kind_of(HANDLE h)
{
    DWORD type, mode;
    if (!h || h == GRT_INVALID_HANDLE) return GRT_KIND_OTHER;
    type = GetFileType(h);
    if (type == GRT_FILE_TYPE_DISK) return GRT_KIND_DISK;
    if (type == GRT_FILE_TYPE_PIPE) return GRT_KIND_PIPE;
    if (type == GRT_FILE_TYPE_CHAR) return GetConsoleMode(h, &mode) ? GRT_KIND_CONSOLE : GRT_KIND_DEVICE;
    return GRT_KIND_OTHER;
}

static void bind(struct Descriptor *d, HANDLE h, int mode)
{
    d->handle = h;
    d->mode = mode;
    d->kind = kind_of(h);
    d->append = 0;
    d->ended = 0;
    d->aheadAt = d->aheadEnd = 0;
}

/* 0, 1 and 2: the standard handles, in text mode, as a console program starts */
void __grt_files_start(void)
{
    bind(&first[0], GetStdHandle(GRT_STD_INPUT), GRT_TEXT);
    bind(&first[1], GetStdHandle(GRT_STD_OUTPUT), GRT_TEXT);
    bind(&first[2], GetStdHandle(GRT_STD_ERROR), GRT_TEXT);
}

int __grt_descriptor(int fd)
{
    struct Descriptor *d = open_descriptor(fd);
    if (!d) return -1;
    return d->mode | d->kind | (d->append ? GRT_APPEND : 0);
}

/* every byte to Windows; 0 with errno set if it would not take them */
static int put(struct Descriptor *d, const char *bytes, unsigned count)
{
    if (d->append) SetFilePointerEx(d->handle, 0, 0, 2);
    while (count > 0) {
        DWORD wrote = 0;
        if (!WriteFile(d->handle, bytes, count, &wrote, 0)) { __grt_set_errno_from(GetLastError()); return 0; }
        if (wrote == 0) { errno = ENOSPC; return 0; }
        bytes += wrote;
        count -= wrote;
    }
    return 1;
}

int __grt_write_raw(int fd, const char *bytes, unsigned count)
{
    int held, result;
    struct Descriptor *d = take(fd, &held);
    if (!d) return -1;
    result = put(d, bytes, count) ? (int)count : -1;
    give(d, held);
    return result;
}

/* _write's bytes, LF as CR LF in text mode */
static int write_text(struct Descriptor *d, const char *bytes, unsigned count)
{
    char staged[1024];
    unsigned i, n = 0;
    if (d->mode == GRT_BINARY) return put(d, bytes, count) ? (int)count : -1;
    for (i = 0; i < count; i++) {
        if (bytes[i] == '\n') staged[n++] = '\r';
        staged[n++] = bytes[i];
        if (n >= sizeof staged - 1) {
            if (!put(d, staged, n)) return -1;
            n = 0;
        }
    }
    if (n > 0 && !put(d, staged, n)) return -1;
    return (int)count;
}

int __grt_write(int fd, const char *bytes, unsigned count)
{
    int held, result;
    struct Descriptor *d = take(fd, &held);
    if (!d) return -1;
    result = write_text(d, bytes, count);
    give(d, held);
    return result;
}

/* bytes as Windows gives them: 0 at the end (a pipe whose writer has gone included), -1 on an error */
static int from_windows(struct Descriptor *d, char *bytes, unsigned count)
{
    DWORD got = 0;
    if (!ReadFile(d->handle, bytes, count, &got, 0)) {
        DWORD e = GetLastError();
        if (e == GRT_ERROR_BROKEN_PIPE) return 0;
        __grt_set_errno_from(e);
        return -1;
    }
    return (int)got;
}

int __grt_read_raw(int fd, char *bytes, unsigned count)
{
    int held, result;
    struct Descriptor *d = take(fd, &held);
    unsigned n, i;
    if (!d) return -1;
    if (d->aheadAt < d->aheadEnd) {            /* left by a _read-style read */
        n = d->aheadEnd - d->aheadAt;
        if (n > count) n = count;
        for (i = 0; i < n; i++) bytes[i] = (char)d->ahead[d->aheadAt + i];
        d->aheadAt += n;
        result = (int)n;
    } else {
        result = from_windows(d, bytes, count);
    }
    give(d, held);
    return result;
}

/* _read's bytes: a handle that cannot seek through the descriptor's own buffer */
static int raw(struct Descriptor *d, char *bytes, unsigned count)
{
    unsigned n, i;
    int got;
    if (d->kind == GRT_KIND_DISK) return from_windows(d, bytes, count);
    if (d->aheadAt == d->aheadEnd) {
        if (!d->ahead) {
            d->ahead = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, AHEAD);
            if (!d->ahead) { errno = ENOMEM; return -1; }
        }
        got = from_windows(d, (char *)d->ahead, AHEAD);
        d->aheadAt = 0;
        d->aheadEnd = got > 0 ? (unsigned)got : 0;
        if (got <= 0) return got;
    }
    n = d->aheadEnd - d->aheadAt;
    if (n > count) n = count;
    for (i = 0; i < n; i++) bytes[i] = (char)d->ahead[d->aheadAt + i];
    d->aheadAt += n;
    return (int)n;
}

/* the one byte raw() just gave, given back */
static void unread(struct Descriptor *d)
{
    if (d->kind == GRT_KIND_DISK) SetFilePointerEx(d->handle, -1, 0, 1);
    else d->aheadAt--;
}

/* _read's bytes, CR LF as LF in text mode, CTRL+Z the end of a file or a pipe */
static int read_text(struct Descriptor *d, char *bytes, unsigned count)
{
    int n, i, out = 0;
    if (count == 0) return 0;
    if (d->mode == GRT_BINARY) return raw(d, bytes, count);
    if (d->ended) return 0;
    n = raw(d, bytes, count);
    if (n <= 0) return n;
    for (i = 0; i < n; i++) {
        char c = bytes[i];
        if (c == CTRL_Z && (d->kind == GRT_KIND_DISK || d->kind == GRT_KIND_PIPE)) { d->ended = 1; break; }
        if (c != '\r') { bytes[out++] = c; continue; }
        if (i + 1 < n) {
            if (bytes[i + 1] == '\n') { bytes[out++] = '\n'; i++; }
            else bytes[out++] = '\r';
            continue;
        }
        /* a CR that ends what was read: an LF after it makes the two one LF */
        {
            char next;
            if (raw(d, &next, 1) == 1) {
                if (next == '\n') { bytes[out++] = '\n'; continue; }
                unread(d);
            }
            bytes[out++] = '\r';
        }
    }
    return out;
}

int __grt_read(int fd, char *bytes, unsigned count)
{
    int held, result;
    struct Descriptor *d = take(fd, &held);
    if (!d) return -1;
    result = read_text(d, bytes, count);
    give(d, held);
    return result;
}

WCHAR *__grt_widen(const char *path)
{
    WCHAR *wide;
    int n = MultiByteToWideChar(GRT_CP_ACP, 0, path, -1, 0, 0);
    if (n <= 0) { __grt_set_errno_from(GetLastError()); return 0; }
    wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (unsigned long long)n * sizeof(WCHAR));
    if (!wide) { errno = ENOMEM; return 0; }
    MultiByteToWideChar(GRT_CP_ACP, 0, path, -1, wide, n);
    return wide;
}

/* a free descriptor from 3 up, the table grown by a block when it is full; -1 with errno EMFILE */
static int claim(void)
{
    int b, i;
    for (b = 0; b < BLOCKS; b++) {
        if (!blocks[b]) {
            blocks[b] = (struct Descriptor *)HeapAlloc(GetProcessHeap(), GRT_HEAP_ZERO_MEMORY,
                                                       BLOCK * sizeof(struct Descriptor));
            if (!blocks[b]) { errno = ENOMEM; return -1; }
        }
        for (i = b == 0 ? 3 : 0; i < BLOCK; i++)
            if (blocks[b][i].mode == 0) {
                blocks[b][i].mode = CLAIMED;
                return b * BLOCK + i;
            }
    }
    errno = 24;                                                 /* EMFILE */
    return -1;
}

/* claim() under the table's lock: the descriptor is the caller's until bind() opens it or mode 0 frees it */
static int free_descriptor(void)
{
    int fd, held = __grt_threads;
    if (held) AcquireSRWLockExclusive(&table);
    fd = claim();
    if (held) ReleaseSRWLockExclusive(&table);
    return fd;
}

int __grt_open(const char *path, unsigned flags)
{
    WCHAR *wide;
    DWORD access, disposition;
    HANDLE h;
    int fd = free_descriptor();
    struct Descriptor *d;
    if (fd < 0) return -1;
    d = slot_of(fd);
    wide = __grt_widen(path);
    if (!wide) { d->mode = 0; return -1; }
    access = (flags & 3) == 0 ? GRT_GENERIC_READ : (flags & 3) == 1 ? GRT_GENERIC_WRITE : GRT_GENERIC_READ | GRT_GENERIC_WRITE;
    if ((flags & 0x200) && (flags & 0x400)) disposition = GRT_CREATE_ALWAYS;
    else if (flags & 0x200) disposition = GRT_OPEN_ALWAYS;
    else if (flags & 0x400) disposition = GRT_TRUNCATE_EXISTING;
    else disposition = GRT_OPEN_EXISTING;
    h = CreateFileW(wide, access, GRT_SHARE_READ_WRITE, 0, disposition, GRT_FILE_ATTRIBUTE_NORMAL, 0);
    if (h == GRT_INVALID_HANDLE) __grt_set_errno_from(GetLastError());
    HeapFree(GetProcessHeap(), 0, wide);
    if (h == GRT_INVALID_HANDLE) { d->mode = 0; return -1; }
    bind(d, h, (flags & 0x8000) ? GRT_BINARY : GRT_TEXT);
    d->append = (flags & 8) != 0;
    return fd;
}

int __grt_close(int fd)
{
    int held, ok;
    struct Descriptor *d = take(fd, &held);
    if (!d) return -1;
    ok = CloseHandle(d->handle);
    if (!ok) __grt_set_errno_from(GetLastError());
    if (d->ahead) HeapFree(GetProcessHeap(), 0, d->ahead);
    d->ahead = 0;
    d->handle = 0;
    d->mode = 0;            /* last: from here an open may claim it */
    give(d, held);
    return ok ? 0 : -1;
}

static long long seek_to(struct Descriptor *d, long long offset, int origin)
{
    long long at = 0;
    if (origin < 0 || origin > 2) { errno = EINVAL; return -1; }
    if (d->kind != GRT_KIND_DISK) { errno = EINVAL; return -1; }
    if (!SetFilePointerEx(d->handle, offset, &at, (DWORD)origin)) { __grt_set_errno_from(GetLastError()); return -1; }
    d->ended = 0;
    return at;
}

long long __grt_seek64(int fd, long long offset, int origin)
{
    int held;
    long long at;
    struct Descriptor *d = take(fd, &held);
    if (!d) return -1;
    at = seek_to(d, offset, origin);
    give(d, held);
    return at;
}

long __grt_seek(int fd, long offset, int origin)
{
    long long at = __grt_seek64(fd, offset, origin);
    if (at < 0) return -1L;
    if (at > 2147483647LL) { errno = EINVAL; return -1L; }
    return (long)at;
}

int __grt_unlink(const char *path)
{
    WCHAR *wide = __grt_widen(path);
    int ok;
    if (!wide) return -1;
    ok = DeleteFileW(wide);
    if (!ok) __grt_set_errno_from(GetLastError());
    HeapFree(GetProcessHeap(), 0, wide);
    return ok ? 0 : -1;
}

int __grt_rename(const char *from, const char *to)
{
    WCHAR *a = __grt_widen(from), *b;
    int ok;
    if (!a) return -1;
    b = __grt_widen(to);
    if (!b) { HeapFree(GetProcessHeap(), 0, a); return -1; }
    ok = MoveFileExW(a, b, GRT_MOVEFILE_COPY_ALLOWED);
    if (!ok) __grt_set_errno_from(GetLastError());
    HeapFree(GetProcessHeap(), 0, a);
    HeapFree(GetProcessHeap(), 0, b);
    return ok ? 0 : -1;
}

/* Spec: Microsoft C runtime, _setmode - the descriptor's translation mode, _O_TEXT or _O_BINARY; the
   one it had, or -1 with errno EBADF (not open) or EINVAL (another mode). */
int _setmode(int fd, int mode)
{
    int held, old;
    struct Descriptor *d = take(fd, &held);
    if (!d) return -1;
    if (mode != GRT_TEXT && mode != GRT_BINARY) {
        give(d, held);
        errno = EINVAL;
        return -1;
    }
    old = d->mode;
    d->mode = mode;
    give(d, held);
    return old;
}

/* Spec: Microsoft C runtime, _isatty - non-zero when the descriptor is a character device (NUL included) */
int _isatty(int fd)
{
    struct Descriptor *d = open_descriptor(fd);
    if (!d) return 0;
    return d->kind == GRT_KIND_CONSOLE || d->kind == GRT_KIND_DEVICE;
}

void __grt_say(const char *message)
{
    unsigned n = 0;
    while (message[n]) n++;
    __grt_write(2, message, n);
}
