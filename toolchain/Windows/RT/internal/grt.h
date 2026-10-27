/* grt.h - what the parts of GRIDE's runtime for Windows say to each other (docs\Runtime_Design.md 4.2).
 *
 * The libraries are layers, each calling only itself and the layers below it:
 *   grtstart   mainCRTStartup, the initialiser tables, __chkstk, _fltused
 *   grtcompat  legacy and Microsoft-compatibility names (32-bit time, POSIX names, /GS)
 *   grtcxx     C++ support
 *   grtc       the ISO C library: RTS6x's, and the Windows stream layer (c\Stream*.cpp)
 *   grtwin     the Windows layer, the only importer of kernel32: descriptors, the heap, errno's storage,
 *              the environment and argv, the clock and the time zone, locks, the process's end
 *
 * A descriptor is RTS6x's: 0, 1 and 2 the standard handles, 3 up the files. Each has a translation mode,
 * _O_TEXT or _O_BINARY as <fcntl.h> numbers them. __grt_read and __grt_write translate in text mode, as
 * the Microsoft runtime's _read and _write do. The streams read and write raw bytes and translate in their
 * own buffers, so that a stream's position is always known without asking Windows. */
#ifndef GRT_H
#define GRT_H
#ifdef __cplusplus
extern "C" {
#endif

#define GRT_TEXT   0x4000
#define GRT_BINARY 0x8000
#define GRT_DESCRIPTORS 8192        /* the Microsoft runtime's limit */

/* What __grt_descriptor tells about an open descriptor, besides GRT_TEXT or GRT_BINARY */
#define GRT_KIND_MASK    0x0F
#define GRT_KIND_DISK    1          /* a file: it can seek */
#define GRT_KIND_DEVICE  2          /* a character device that is not a console (NUL, a serial port) */
#define GRT_KIND_PIPE    3
#define GRT_KIND_CONSOLE 4          /* a character device on which GetConsoleMode succeeds */
#define GRT_KIND_OTHER   5
#define GRT_APPEND       0x10       /* every write goes to the end */

/* ---- grtwin: win\files.c, the descriptors. Each answers -1 on failure, errno set. */
void __grt_files_start(void);
/* GRT_TEXT or GRT_BINARY, a GRT_KIND_ and GRT_APPEND; -1 (errno EBADF) when fd is not open */
int  __grt_descriptor(int fd);
/* _write and _read: text translated in text mode */
int  __grt_write(int fd, const char *bytes, unsigned count);
int  __grt_read(int fd, char *bytes, unsigned count);
/* bytes as they are: every byte written (count) or -1; a read's count, 0 at the end, -1 on an error */
int  __grt_write_raw(int fd, const char *bytes, unsigned count);
int  __grt_read_raw(int fd, char *bytes, unsigned count);
/* flags as RTS6x's CioChannel spells them: 0 read, 1 write, 2 both, 8 append, 0x200 create,
   0x400 truncate, 0x8000 binary */
int  __grt_open(const char *path, unsigned flags);
int  __grt_close(int fd);
long __grt_seek(int fd, long offset, int origin);
long long __grt_seek64(int fd, long long offset, int origin);
int  __grt_unlink(const char *path);
int  __grt_rename(const char *from, const char *to);
/* errno for a Windows error number, as the Microsoft runtime maps it */
void __grt_set_errno_from(unsigned long code);
/* path in UTF-16 from the ANSI code page, in memory from the process heap (the caller frees it with HeapFree);
   0 with errno set */
unsigned short *__grt_widen(const char *path);
/* a message on stderr, unbuffered, for the runtime's own errors */
void __grt_say(const char *message);

/* ---- grtwin: win\getcwd.c */
/* the working directory's drive, 1 for A:, 2 for B: ...; 0 when it has no drive letter (a share's path) */
int  __grt_drive(void);

/* ---- grtwin: win\environment.c */
char **__grt_environment(void);
int  __grt_getenv(const char *name, char *value, unsigned capacity);
void __grt_arguments(int *argc, char ***argv);

/* ---- grtwin: win\time.c, Windows' clock and time zone */
long long __grt_time_now(void);          /* seconds since 1970-01-01 00:00:00 UTC */
long long __grt_clock_ms(void);          /* milliseconds since the program started */
void __grt_clock_start(void);
/* t (seconds since 1970) as local time; 1 if daylight saving time is in force. 0 on failure. */
int  __grt_local_fields(long long t, int fields[9]);
/* local time's fields to seconds since 1970; -1 when Windows cannot convert them */
long long __grt_from_local(const int fields[9], int isdst);
/* a file's time (FILETIME's ticks, UTC) as _stat and _findfirst report it: made local by Windows, then back as
   mktime takes local time; -1 when Windows cannot convert it, and -1 with errno EINVAL outside 1970..3000 */
long long __grt_file_time(unsigned long low, unsigned long high);
/* the time zone's name and its offset east of UTC in minutes, for strftime's %Z and %z */
const char *__grt_zone_name(int isdst);
long __grt_zone_minutes(int isdst);

/* ---- grtwin: win\lock.c, a lock and a condition, each a pointer-sized word that starts zero */
void __grt_lock(void **lock);
void __grt_unlock(void **lock);
void __grt_wait(void **condition, void **lock);
void __grt_wake_all(void **condition);

/* ---- grtwin: win\thread.c, CreateThread over kernel32's */
/* non-zero once the program has started a second thread: from then on the descriptors, the streams and the atexit
   list take their locks, each call deciding once, at its start, whether it locks; a program with one thread takes
   none */
extern int __grt_threads;

/* ---- grtwin: win\halt.c, the process's end */
/* ExitProcess with the status (RTS6x's Termination calls this once its list is done) */
void __rts6x_halt(int status);
/* the end abort, a failed assert and an uncaught exception take: the fast fail of a Release program,
   exit code 0xC0000409, as the Microsoft runtime's */
void __grt_fail_fast(void);

/* ---- grtc: c\StreamOpen.cpp, the standard streams set up at start */
void __grt_streams_start(void);
/* ---- grtc: c\memory.asm, the processor's features memcpy and memset read (fast strings), found once */
void __grt_memory_start(void);

#ifdef __cplusplus
}
#endif
#endif
