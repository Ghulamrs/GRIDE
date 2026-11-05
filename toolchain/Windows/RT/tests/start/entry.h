/* entry.h - what the probes of wmain, WinMain and wWinMain share, in C for c90 and cpp11 both (docs\Runtime_Design.md 7).
 *
 * A program run with nothing set is the parent: it starts itself once for each row of rows[] by CreateProcessW, so that
 * a child's command line, environment, show command and handles are exactly the row's, and prints what each child ends
 * with. A child finds GRT_ENTRY in its environment; its entry prints what start-up gave it, then ends as the row says.
 * In a command line or an environment \1 is U+263A, \2 U+00E9 and \3 U+1F600, which the sources cannot spell the same
 * way for both compilers, and each string of an environment ends in a newline. */
#include <windows.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
extern char __ImageBase[];
/*  0 cb  8 lpReserved 16 lpDesktop 24 lpTitle 32 dwX .. 56 dwFillAttribute 60 dwFlags 64 wShowWindow 66 cbReserved2
   72 lpReserved2 80 hStdInput 88 hStdOutput 96 hStdError - 104 bytes */
typedef struct {
    DWORD cb;
    WCHAR *reserved, *desktop, *title;
    DWORD x, y, xSize, ySize, xCount, yCount, fill, flags;
    WORD show, reservedSize;
    BYTE *reserved2;
    HANDLE input, output, error;
} Startup;
typedef struct {
    HANDLE process, thread;
    DWORD processId, threadId;
} Process;
HANDLE GetStdHandle(DWORD which);
BOOL CreateProcessW(LPCWSTR application, LPWSTR line, void *processSecurity, void *threadSecurity, BOOL inherit,
                    DWORD flags, void *environment, LPCWSTR directory, Startup *startup, Process *process);
BOOL GetExitCodeProcess(HANDLE process, DWORD *code);
DWORD GetModuleFileNameW(void *module, LPWSTR name, DWORD size);
#ifdef __cplusplus
}
#endif

#define STARTF_USESHOWWINDOW 1
#define STARTF_USESTDHANDLES 0x100
#define DETACHED_PROCESS 8
#define CREATE_UNICODE_ENVIRONMENT 0x400

/* quiet: no standard handles; show: the show command, or -1 for none; the environment is GRT_ENTRY=mode, then common */
struct Row { const char *mode; const char *line; int quiet; int show; };
static const char common[] = "Zed=last\n=C:=C:\\skipped\nalpha=\2\1\n";

#define ROW_ARGUMENTS \
    { "args", "prog a b c", 0, -1 }, \
    { "args", "prog", 0, -1 }, \
    { "args", "", 0, -1 }, \
    { "args", "\"quoted prog\" one", 0, -1 }, \
    { "args", "\"half\"way prog\tnext", 0, -1 }, \
    { "args", "   leading spaces", 0, -1 }, \
    { "args", "prog\t\ttabs  \"t\tq\"  ", 0, -1 }, \
    { "args", "prog a\\b a\\\\b \"c d\" e\\\"f \"g\\\\\" h\\\\\\\"i \"j\"\"k\" \"\" l\"m n\"o", 0, -1 }, \
    { "args", "prog \1 \2 \3 \"x \1 y\"", 0, -1 }, \
    { "args", "prog 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20", 0, -1 }, \
    { "fall", "prog", 0, -1 }, \
    { "exit", "prog", 0, -1 }, \
    { "fault", "prog", 0, -1 }, \
    { "quiet", "prog", 1, -1 }

#define ROW_SHOW \
    { "args", "prog x", 0, SW_HIDE }, \
    { "args", "prog x", 0, SW_SHOWMINIMIZED }, \
    { "args", "prog x", 0, 7 }

#define ROWS(table) (sizeof table / sizeof table[0])

static void show_character(unsigned c)
{
    if (c >= 32 && c < 127) putchar((int)c);
    else printf("<%x>", c);
}

static void show_narrow(const char *label, const char *s)
{
    printf("%s [", label);
    for (; *s; s++) show_character((unsigned char)*s);
    printf("]\n");
}

static void show_wide(const char *label, const wchar_t *s)
{
    printf("%s [", label);
    for (; *s; s++) show_character(*s);
    printf("]\n");
}

/* a row's bytes in UTF-16: \1 \2 \3 as above, and a newline the end of a string; where the result's end is */
static wchar_t *widen(const char *from, wchar_t *to)
{
    for (; *from; from++)
        switch (*from) {
        case 1: *to++ = 0x263A; break;
        case 2: *to++ = 0xE9; break;
        case 3: *to++ = 0xD83D; *to++ = 0xDE00; break;
        case '\n': *to++ = 0; break;
        default: *to++ = (wchar_t)(unsigned char)*from; break;
        }
    *to = 0;
    return to;
}

static wchar_t own[300];

static void run(const struct Row *row)
{
    static wchar_t line[300], environment[300];
    wchar_t *e = environment;
    Startup startup;
    Process process;
    DWORD code = 0;
    FILE *f;
    int c;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    if (row->show >= 0) {
        startup.flags |= STARTF_USESHOWWINDOW;
        startup.show = (WORD)row->show;
    }
    if (!row->quiet) {
        startup.flags |= STARTF_USESTDHANDLES;
        startup.input = GetStdHandle((DWORD)-10);
        startup.output = GetStdHandle((DWORD)-11);
        startup.error = GetStdHandle((DWORD)-12);
    }
    widen(row->line, line);
    e = widen("GRT_ENTRY=", e);
    e = widen(row->mode, e);
    e = widen("\n", e);
    e = widen(common, e);
    *e = 0;
    show_narrow("line", row->line);
    printf("mode %s, show %d\n", row->mode, row->show);
    fflush(stdout);
    if (!CreateProcessW(own, line, 0, 0, row->quiet ? 0 : 1, CREATE_UNICODE_ENVIRONMENT | (row->quiet ? DETACHED_PROCESS : 0),
                        environment, 0, &startup, &process)) {
        printf("CreateProcessW failed: %lu\n", GetLastError());
        return;
    }
    WaitForSingleObject(process.process, INFINITE);
    GetExitCodeProcess(process.process, &code);
    CloseHandle(process.process);
    CloseHandle(process.thread);
    if (row->quiet) {
        f = fopen("entry.out", "r");
        if (f) {
            while ((c = getc(f)) != EOF) putchar(c);
            fclose(f);
            remove("entry.out");
        }
    }
    printf("exit %lu\n\n", code);
}

static int parent(const struct Row *table, unsigned rows)
{
    unsigned i;
    GetModuleFileNameW(0, own, 300);
    for (i = 0; i < rows; i++) run(&table[i]);
    return 0;
}

/* argv[0] of an empty command line is the program's path, which differs between the two builds */
static void show_argument(const char *label, const wchar_t *s)
{
    const wchar_t *o = own, *t = s;
    GetModuleFileNameW(0, own, 300);
    while (*t && *t == *o) { t++; o++; }
    if (*t == *o) printf("%s the program's path\n", label);
    else show_wide(label, s);
}

static void show_narrow_argument(const char *label, const char *s)
{
    if (strstr(s, ".exe")) printf("%s the program's path\n", label);
    else show_narrow(label, s);
}

static void at_end(void)
{
    printf("atexit\n");
}

static void on_segv(int number)
{
    printf("signal %d\n", number);
    exit(11);
}

static int quiet(void)
{
    FILE *f = fopen("entry.out", "w");
    int n;
    if (!f) return 99;
    fprintf(f, "descriptors %d %d %d, file %d\n", _fileno(stdin), _fileno(stdout), _fileno(stderr), _fileno(f));
    errno = 0;
    n = printf("to nowhere\n");
    fprintf(f, "printf %d errno %d\n", n, errno);
    errno = 0;
    n = fflush(stdout);
    fprintf(f, "fflush %d errno %d error %d\n", n, errno, ferror(stdout) != 0);
    clearerr(stdout);
    errno = 0;
    n = fprintf(stderr, "to nowhere\n");
    fprintf(f, "fprintf %d errno %d error %d\n", n, errno, ferror(stderr) != 0);
    clearerr(stderr);
    errno = 0;
    n = fputs("to nowhere\n", stderr);
    fprintf(f, "fputs %d errno %d error %d\n", n, errno, ferror(stderr) != 0);
    clearerr(stderr);
    errno = 0;
    n = fputc('x', stderr);
    fprintf(f, "fputc %d errno %d error %d\n", n, errno, ferror(stderr) != 0);
    errno = 0;
    n = getchar();
    fprintf(f, "getchar %d errno %d eof %d error %d\n", n, errno, feof(stdin) != 0, ferror(stdin) != 0);
    fclose(f);
    printf("left in the buffer at the end\n");
    return 5;
}

/* What a child does once its entry has shown its arguments. Non-zero: the entry returns it. Zero: the entry falls off
   its end. */
static int child(void)
{
    const char *mode = getenv("GRT_ENTRY");
    if (strcmp(mode, "quiet") == 0) return quiet();
    printf("Zed %s, ZED %s, none %s\n", getenv("Zed"), getenv("ZED"), getenv("none") ? "found" : "(null)");
    show_narrow("alpha", getenv("alpha"));
    if (strcmp(mode, "fall") == 0) {
        fflush(stdout);
        return 0;
    }
    if (strcmp(mode, "exit") == 0) {
        atexit(at_end);
        printf("left in the buffer for exit\n");
        exit(9);
    }
    if (strcmp(mode, "fault") == 0) {
        signal(SIGSEGV, on_segv);
        fflush(stdout);
        *(volatile int *)0 = 1;
    }
    return 7;
}

#ifdef __cplusplus
/* made before the entry, with __argc already set, and destroyed after the atexit functions registered later */
struct Probe {
    static int shown() { const char *m = getenv("GRT_ENTRY"); return m && strcmp(m, "quiet") != 0; }
    Probe() { if (shown()) printf("static object made, %d arguments\n", __argc); }
    ~Probe() { if (shown()) printf("static object destroyed\n"); }
};
static Probe probe;
#endif
