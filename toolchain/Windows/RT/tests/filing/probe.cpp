// probe.cpp - the filing component's probe (docs\Runtime_Design.md 11.2): _stat64i32, _findfirst64i32,
// _findnext64i32, _findclose, _getcwd and system, called through cpp11's <sys/stat.h>, <io.h>, <direct.h> and
// <stdlib.h> as cppparse calls them, each answer printed with errno. The same object linked with GRIDE's runtime
// and with the UCRT must print the same lines; filing.ps1 makes the files it looks at, runs it and compares.
//
//   probe stat <drive letter>   probe find   probe cwd <the expected directory>   probe system
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>

static char drive;

static int zeroed(const struct _stat64i32 *s)
{
    return s->st_dev == 0 && s->st_ino == 0 && s->st_mode == 0 && s->st_nlink == 0 && s->st_uid == 0 &&
           s->st_gid == 0 && s->st_rdev == 0 && s->st_size == 0 && s->st_atime == 0 && s->st_mtime == 0 &&
           s->st_ctime == 0;
}

// What is printed of a found file: its size and times only where the script fixed them, so that the transcript
// is the same on every machine (a directory's size, the echo program's, the drive's root's times are not).
enum { SIZE = 1, TIMES = 2 };

static void stat_one(const char *path, const char *label, int shown)
{
    struct _stat64i32 s;
    memset(&s, 0x5A, sizeof s);
    errno = 0;
    int r = _stat64i32(path, &s);
    printf("stat \"%s\": %d errno %d", label ? label : path, r, errno);
    if (r == 0) {
        printf(" mode %o nlink %d ino %u uid %d gid %d", s.st_mode, s.st_nlink, s.st_ino, s.st_uid, s.st_gid);
        if (shown & SIZE) printf(" size %ld", s.st_size);
        if (s.st_dev == (unsigned)(drive - 'A')) printf(" dev the drive");
        else printf(" dev %d", (int)s.st_dev);
        printf(s.st_rdev == s.st_dev ? " rdev =dev" : " rdev %d", (int)s.st_rdev);
        if (shown & TIMES) printf(" m %lld a %lld c %lld", s.st_mtime, s.st_atime, s.st_ctime);
    } else
        printf(zeroed(&s) ? " zeroed" : " not zeroed");
    printf("\n");
}

static void stats(void)
{
    char root[4] = { drive, ':', '\\', 0 }, bare[3] = { drive, ':', 0 };
    static const char *const files[] = { "file.txt", "normal.txt", "readonly.txt", "prog.EXE", "run.Cmd", "x.cmd.txt",
                                         "sub/inner.txt", "sub\\inner.txt", "./file.txt", "link1.txt", "link2.txt",
                                         "old.txt", "junc/inner.txt", "NUL" };
    static const char *const others[] = { "sub", "sub/", "sub\\", ".", "..", "junc", "missing.txt", "nodir/missing.txt",
                                          "*.txt", "", "file.txt/", "file.txt\\", "big.bin" };
    unsigned i;
    for (i = 0; i < sizeof files / sizeof files[0]; i++) stat_one(files[i], 0, SIZE | TIMES);
    stat_one("bin2/cmd.exe", 0, TIMES);
    for (i = 0; i < sizeof others / sizeof others[0]; i++) stat_one(others[i], 0, 0);
    stat_one(root, "<drive>:\\", 0);
    stat_one(bare, "<drive>:", 0);
}

static void find_all(const char *pattern)
{
    struct _finddata64i32_t f;
    memset(&f, 0x5A, sizeof f);
    errno = 0;
    intptr_t h = _findfirst64i32(pattern, &f);
    printf("findfirst \"%s\": %s errno %d\n", pattern, h == -1 ? "-1" : "a handle", errno);
    if (h == -1) {
        printf("  entry %s\n", f.attrib == 0x5A5A5A5Au && f.name[0] == 0x5A ? "untouched" : "changed");
        return;
    }
    int r = 0;
    while (r == 0) {
        printf("  \"%s\" attrib 0x%x", f.name, f.attrib);
        if (!(f.attrib & _A_SUBDIR))
            printf(" size %lu c %lld a %lld w %lld", (unsigned long)f.size, f.time_create, f.time_access, f.time_write);
        printf("\n");
        errno = 0;
        r = _findnext64i32(h, &f);
    }
    printf("  findnext %d errno %d\n", r, errno);
    errno = 0;
    r = _findnext64i32(h, &f);
    printf("  findnext again %d errno %d\n", r, errno);
    errno = 0;
    r = _findclose(h);
    printf("  findclose %d errno %d\n", r, errno);
}

static void finds(void)
{
    static const char *const patterns[] = { "*", "sub/*", "sub\\*.txt", "*.TXT", "junc/*", "missing/*", "*.nomatch",
                                            "a|b", "", "file.txt", "sub" };
    unsigned i;
    for (i = 0; i < sizeof patterns / sizeof patterns[0]; i++) find_all(patterns[i]);
    errno = 0;
    int r = _findclose(0);
    printf("findclose(0) %d errno %d\n", r, errno);
}

static const char *same(const char *got, const char *expected)
{
    return !got ? "a null pointer" : strcmp(got, expected) == 0 ? "the directory" : got;
}

static void cwds(const char *expected)
{
    char buf[1024];
    int n = (int)strlen(expected);
    char *r;
    int sizes[5] = { (int)sizeof buf, n + 1, n, 1, 2 };
    unsigned i;
    for (i = 0; i < 5; i++) {
        memset(buf, 'x', sizeof buf);
        errno = 0;
        r = _getcwd(buf, sizes[i]);
        printf("getcwd(buf, %s): %s%s errno %d buf[0] %d\n",
               i == 0 ? "1024" : i == 1 ? "length+1" : i == 2 ? "length" : i == 3 ? "1" : "2",
               same(r, expected), r && r != buf ? " (not buf)" : "", errno, buf[0]);
    }
    for (i = 0; i < 3; i++) {
        int size = i == 0 ? 0 : i == 1 ? 1 : 4096;
        errno = 0;
        r = _getcwd(0, size);
        printf("getcwd(0, %d): %s errno %d\n", size, same(r, expected), errno);
        free(r);
    }
}

static void run(const char *command)
{
    fflush(stdout);
    errno = 0;
    int r = system(command);
    if (command) printf("system(\"%s\"): %d errno %d\n", command, r, errno);
    else printf("system(NULL): %d errno %d\n", r, errno);
}

static void systems(void)
{
    run(0);
    run("one two 7");
    run("\"quoted words\" 300");
    run("");
    run("-1");
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: probe stat <drive> | find | cwd <directory> | system\n"); return 2; }
    if (strcmp(argv[1], "stat") == 0 && argc > 2) { drive = argv[2][0]; stats(); }
    else if (strcmp(argv[1], "find") == 0) finds();
    else if (strcmp(argv[1], "cwd") == 0 && argc > 2) cwds(argv[2]);
    else if (strcmp(argv[1], "system") == 0) systems();
    else { printf("probe: %s?\n", argv[1]); return 2; }
    return 0;
}
