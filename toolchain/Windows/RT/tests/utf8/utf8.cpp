// utf8.cpp - a program's narrow strings as UTF-8, and as they were before it asks (docs\Runtime_Design.md 6). One
// object linked with GRIDE's runtime and with the UCRT, each run in an empty folder (probe.ps1): the transcript is
// the UCRT's, utf8.expected, in hexadecimal so that it reads the same under any code page, with one raw line.
// What is shown:
// - a program that does not ask: names and text in the ANSI code page, a byte the wide character of its value;
// - setlocale(LC_CTYPE, ".UTF-8"): file names past the ANSI code page through every call that takes a path, as Windows
//   itself reads them back; a name that is not UTF-8 refused; MB_CUR_MAX, the conversions, printf's %ls and %lc, wide
//   output to a text file;
// - setlocale(LC_CTYPE, "C"): the program as it was.
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <wchar.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>

extern "C" int MultiByteToWideChar(unsigned page, unsigned long flags, const char *bytes, int count, wchar_t *wide, int room);
extern "C" int WideCharToMultiByte(unsigned page, unsigned long flags, const wchar_t *wide, int count, char *bytes, int room,
                                   const char *defaultChar, int *usedDefault);
extern "C" int GetFileAttributesExW(const wchar_t *path, int level, void *data);

static const char Greek[] = "\xCE\xB1\xCE\xB2\xCE\xB3.txt";
static const char Japanese[] = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt";
static const char Emoji[] = "\xF0\x9F\x98\x80-smile.txt";
static const char Moved[] = "\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB5\xD1\x85\xD0\xB0\xD0\xBB.txt";
static const char Folder[] = "\xE6\x96\x87\xE4\xBB\xB6\xE5\xA4\xB9";

static void hex(const char *what, const char *s, size_t n)
{
    printf("%s:", what);
    for (size_t i = 0; i < n; i++) printf(" %02x", (unsigned char)s[i]);
    printf("\n");
}

static void units(const char *what, const wchar_t *s, size_t n)
{
    printf("%s:", what);
    for (size_t i = 0; i < n; i++) printf(" %04x", (unsigned)s[i]);
    printf("\n");
}

// the name Windows has for these bytes in a code page: 65001, or 0, the ANSI one
static void wideName(const char *name, unsigned page, wchar_t *wide)
{
    wide[0] = 0;
    MultiByteToWideChar(page, 0, name, -1, wide, 64);
}

static int windowsHas(const wchar_t *wide)
{
    char data[64];
    return GetFileAttributesExW(wide, 0, data) != 0;
}

static void write(const char *name, const char *text)
{
    errno = 0;
    FILE *f = fopen(name, "w");
    printf("fopen write %d errno %d\n", f != 0, errno);
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void readBack(const char *name)
{
    char line[64] = "";
    errno = 0;
    FILE *f = fopen(name, "r");
    printf("fopen read %d errno %d", f != 0, errno);
    if (f) {
        if (fgets(line, sizeof line, f)) printf(" gives %s", line);
        fclose(f);
    }
    printf("\n");
}

// a name through every call that takes a path, each looked at by Windows, whose wide functions take no code page
static void paths(const char *name, unsigned page)
{
    wchar_t wide[64], movedWide[64];
    wideName(name, page, wide);
    wideName(Moved, page, movedWide);
    hex("name", name, strlen(name));
    write(name, "contents\n");
    printf("windows has it %d\n", windowsHas(wide));
    readBack(name);
    struct _stat64i32 s;
    errno = 0;
    int r = _stat64i32(name, &s);
    printf("stat %d errno %d size %ld\n", r, errno, r == 0 ? (long)s.st_size : -1L);
    printf("access %d\n", _access(name, 0));
    errno = 0;
    printf("rename %d errno %d\n", rename(name, Moved), errno);
    printf("windows has it %d, and the new name %d\n", windowsHas(wide), windowsHas(movedWide));
    errno = 0;
    printf("remove %d errno %d\n", remove(Moved), errno);
    printf("windows has the new name %d\n", windowsHas(movedWide));
}

static void folders(unsigned page)
{
    wchar_t wide[64];
    wideName(Folder, page, wide);
    printf("mkdir %d\n", _mkdir(Folder));
    printf("windows has it %d\n", windowsHas(wide));
    printf("chdir %d\n", _chdir(Folder));
    write(Greek, "x\n");
    write(Emoji, "x\n");
    char here[300];
    if (_getcwd(here, sizeof here)) {
        const char *last = strrchr(here, '\\');
        hex("getcwd ends", last ? last + 1 : here, strlen(last ? last + 1 : here));
    }
    struct _finddata64i32_t found;
    long long h = _findfirst64i32("*.txt", &found);
    printf("findfirst %d\n", h != -1);
    if (h != -1) {
        do hex("found", found.name, strlen(found.name));
        while (_findnext64i32(h, &found) == 0);
        _findclose(h);
    }
    remove(Greek);
    remove(Emoji);
    printf("chdir back %d\n", _chdir(".."));
    printf("rmdir %d\n", _rmdir(Folder));
    printf("windows has it %d\n", windowsHas(wide));
}

static void refused(const char *name)
{
    hex("not text", name, strlen(name));
    errno = 0;
    FILE *f = fopen(name, "w");
    printf("fopen %d errno %d\n", f != 0, errno);
    if (f) {
        fclose(f);
        remove(name);
    }
    errno = 0;
    printf("access %d errno %d\n", _access(name, 0), errno);
    errno = 0;
    printf("mkdir %d errno %d\n", _mkdir(name), errno);
}

static const char *const samples[] = { "a", "\xC3\xBC", "\xE6\x97\xA5", "\xF0\x9F\x98\x80", "\xC3", "\xE6\x97", "\xF0\x9F\x98",
                                       "\x80", "\xFF", "\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80", "" };

static void mbState(mbstate_t *s) { memset(s, 0, sizeof *s); }

static void characters()
{
    printf("MB_CUR_MAX %d\n", (int)MB_CUR_MAX);
    for (unsigned k = 0; k < sizeof samples / sizeof samples[0]; k++) {
        const char *s = samples[k];
        size_t length = strlen(s);
        hex("sample", s, length);
        for (size_t n = length ? length : 1; n >= 1; n--) {
            mblen(0, 0);
            errno = 0;
            int r = mblen(s, n);
            printf(" mblen %u: %d errno %d\n", (unsigned)n, r, errno);
            mbtowc(0, 0, 0);
            wchar_t w = 0x7777;
            errno = 0;
            r = mbtowc(&w, s, n);
            printf(" mbtowc %u: %d %04x errno %d\n", (unsigned)n, r, (unsigned)w, errno);
            mbstate_t state;
            mbState(&state);
            w = 0x7777;
            errno = 0;
            size_t z = mbrtowc(&w, s, n, &state);
            printf(" mbrtowc %u: %lld %04x errno %d", (unsigned)n, (long long)z, (unsigned)w, errno);
            if (z == (size_t)-2 && n < length) {
                errno = 0;
                z = mbrtowc(&w, s + n, length - n, &state);
                printf(" then %lld %04x errno %d", (long long)z, (unsigned)w, errno);
            }
            printf("\n");
            mbState(&state);
            errno = 0;
            printf(" mbrlen %u: %lld errno %d\n", (unsigned)n, (long long)mbrlen(s, n, &state), errno);
        }
    }
    const wchar_t wides[] = { 0x41, 0x7F, 0x80, 0xFC, 0xFF, 0x100, 0x7FF, 0x800, 0x65E5, 0xD83D, 0xDE00, 0xFFFD, 0xFFFF };
    for (unsigned k = 0; k < sizeof wides / sizeof wides[0]; k++) {
        char b[8] = "";
        printf("wide %04x\n", (unsigned)wides[k]);
        errno = 0;
        int r = wctomb(b, wides[k]);
        printf(" wctomb %d errno %d", r, errno);
        hex("", b, r > 0 ? (size_t)r : 0);
        mbstate_t state;
        mbState(&state);
        errno = 0;
        size_t z = wcrtomb(b, wides[k], &state);
        printf(" wcrtomb %lld errno %d", (long long)z, errno);
        hex("", b, z <= 4 ? z : 0);
    }
    const int bytes[] = { 0, 0x41, 0x7F, 0x80, 0xC3, 0xFF, EOF };
    for (unsigned k = 0; k < sizeof bytes / sizeof bytes[0]; k++) printf("btowc %d: %d\n", bytes[k], (int)btowc(bytes[k]));
    const wint_t backs[] = { 0, 0x41, 0x7F, 0x80, 0xFC, 0xFF, 0x65E5, WEOF };
    for (unsigned k = 0; k < sizeof backs / sizeof backs[0]; k++) printf("wctob %x: %d\n", (unsigned)backs[k], wctob(backs[k]));
}

static void strings()
{
    const char text[] = "a\xC3\xBC\xE6\x97\xA5\xF0\x9F\x98\x80z";
    const wchar_t wide[] = { 'a', 0xFC, 0x65E5, 0xD83D, 0xDE00, 'z', 0 };
    wchar_t w[16];
    char b[32];
    errno = 0;
    printf("mbstowcs count %lld errno %d\n", (long long)mbstowcs(0, text, 0), errno);
    for (size_t n = 0; n <= 8; n++) {
        wmemset(w, 0x7777, 16);
        errno = 0;
        size_t r = mbstowcs(w, text, n);
        printf("mbstowcs %u: %lld errno %d", (unsigned)n, (long long)r, errno);
        units("", w, 8);
    }
    errno = 0;
    printf("mbstowcs bad %lld errno %d\n", (long long)mbstowcs(w, "a\xFF", 8), errno);
    errno = 0;
    printf("wcstombs count %lld errno %d\n", (long long)wcstombs(0, wide, 0), errno);
    for (size_t n = 0; n <= 13; n++) {
        memset(b, 0x77, sizeof b);
        errno = 0;
        size_t r = wcstombs(b, wide, n);
        printf("wcstombs %u: %lld errno %d", (unsigned)n, (long long)r, errno);
        hex("", b, 13);
    }
    const wchar_t lone[] = { 'a', 0xDE00, 'b', 0 }, loneHigh[] = { 'a', 0xD83D, 'b', 0 };
    errno = 0;
    printf("wcstombs lone low %lld errno %d\n", (long long)wcstombs(b, lone, 16), errno);
    errno = 0;
    printf("wcstombs lone high %lld errno %d\n", (long long)wcstombs(b, loneHigh, 16), errno);

    mbstate_t state;
    for (size_t n = 0; n <= 8; n++) {
        const char *from = text;
        mbState(&state);
        wmemset(w, 0x7777, 16);
        errno = 0;
        size_t r = mbsrtowcs(w, &from, n, &state);
        printf("mbsrtowcs %u: %lld errno %d from %d", (unsigned)n, (long long)r, errno, from ? (int)(from - text) : -1);
        units("", w, 8);
    }
    const char *bad = "ab\xFF";
    mbState(&state);
    errno = 0;
    size_t r = mbsrtowcs(w, &bad, 8, &state);
    printf("mbsrtowcs bad %lld errno %d from %s\n", (long long)r, errno, bad ? "set" : "null");
    for (size_t n = 0; n <= 13; n++) {
        const wchar_t *from = wide;
        mbState(&state);
        memset(b, 0x77, sizeof b);
        errno = 0;
        size_t k = wcsrtombs(b, &from, n, &state);
        printf("wcsrtombs %u: %lld errno %d from %d", (unsigned)n, (long long)k, errno, from ? (int)(from - wide) : -1);
        hex("", b, 13);
    }
    const wchar_t *from = lone;
    mbState(&state);
    errno = 0;
    size_t k = wcsrtombs(b, &from, 16, &state);
    printf("wcsrtombs lone %lld errno %d from %d\n", (long long)k, errno, from ? (int)(from - lone) : -1);
}

static void show(const char *what, int r, const char *b)
{
    printf("%s %d errno %d", what, r, errno);
    hex("", b, strlen(b));
    errno = 0;
}

static void formats()
{
    const wchar_t w[] = { 'a', 0xFC, 0x65E5, 0xD83D, 0xDE00, 0 };
    const wchar_t lowFirst[] = { 'a', 0xDE00, 'b', 0 }, highEnds[] = { 'a', 0xD83D, 0 }, highThen[] = { 0xD83D, 'b', 0 };
    char b[64];
    errno = 0;
    b[0] = 0; show("%ls", snprintf(b, sizeof b, "[%ls]", w), b);
    b[0] = 0; show("%12ls", snprintf(b, sizeof b, "[%12ls]", w), b);
    b[0] = 0; show("%-12ls", snprintf(b, sizeof b, "[%-12ls]", w), b);
    b[0] = 0; show("%.3ls", snprintf(b, sizeof b, "[%.3ls]", w), b);
    b[0] = 0; show("%.4ls", snprintf(b, sizeof b, "[%.4ls]", w), b);
    b[0] = 0; show("%S", snprintf(b, sizeof b, "[%S]", w), b);
    b[0] = 0; show("%lc", snprintf(b, sizeof b, "[%lc]", (wint_t)0xFC), b);
    b[0] = 0; show("%lc", snprintf(b, sizeof b, "[%lc]", (wint_t)0x65E5), b);
    b[0] = 0; show("%5lc", snprintf(b, sizeof b, "[%5lc]", (wint_t)0x65E5), b);
    b[0] = 0; show("%-5lc", snprintf(b, sizeof b, "[%-5lc]", (wint_t)0xFC), b);
    int r = snprintf(b, sizeof b, "[%lc]", (wint_t)0xD83D);     // the answer alone: the UCRT leaves other bytes in b
    printf("%%lc surrogate %d errno %d\n", r, errno);
    errno = 0;
    b[0] = 0; show("%C", snprintf(b, sizeof b, "[%C]", (wint_t)0xFC), b);
    b[0] = 0; show("%ls low first", snprintf(b, sizeof b, "[%ls]", lowFirst), b);
    b[0] = 0; show("%ls high ends", snprintf(b, sizeof b, "[%ls]", highEnds), b);
    b[0] = 0; show("%ls high then", snprintf(b, sizeof b, "[%ls]", highThen), b);
    b[0] = 0; show("%s", snprintf(b, sizeof b, "[%s]", "\xC3\xBC"), b);
}

static void wideStreams()
{
    const wchar_t w[] = { 'a', 0xFC, 0x65E5, '\n', 0 };
    const wchar_t pair[] = { 0xD83D, 0xDE00, '\n', 0 };
    FILE *f = fopen("wide.out", "w");
    errno = 0;
    printf("fputwc %04x errno %d\n", (unsigned)fputwc(0xFC, f), errno);
    errno = 0;
    printf("fputwc %04x errno %d\n", (unsigned)fputwc(0x65E5, f), errno);
    errno = 0;
    printf("fputwc %04x errno %d\n", (unsigned)fputwc(0xD83D, f), errno);
    errno = 0;
    printf("fputws %d errno %d\n", fputws(w, f), errno);
    errno = 0;
    printf("fwprintf %d errno %d\n", fwprintf(f, L"%s|%c|%d\n", w, (wint_t)0x65E5, 5), errno);
    errno = 0;
    printf("fwprintf pair %d errno %d\n", fwprintf(f, L"%s", pair), errno);
    fclose(f);
    char b[128];
    f = fopen("wide.out", "rb");
    size_t n = f ? fread(b, 1, sizeof b, f) : 0;
    if (f) fclose(f);
    hex("wide.out", b, n);
    remove("wide.out");
}

static void all(unsigned page)
{
    paths(Greek, page);
    paths(Japanese, page);
    paths(Emoji, page);
    folders(page);
    characters();
    strings();
    formats();
    wideStreams();
}

int main()
{
    printf("-- not asked: the ANSI code page, a byte the wide character of its value\n");
    printf("ctype %s all %s\n", setlocale(LC_CTYPE, 0), setlocale(LC_ALL, 0));
    wchar_t wide[64];
    const wchar_t cafe[] = { 'c', 'a', 'f', 0xE9, '.', 't', 'x', 't', 0 };
    char ansi[64];
    WideCharToMultiByte(0, 0, cafe, -1, ansi, sizeof ansi, 0, 0);
    wideName(ansi, 0, wide);
    write(ansi, "ansi\n");
    printf("windows has the ANSI name %d\n", windowsHas(cafe));
    readBack(ansi);
    remove(ansi);
    all(0);

    printf("-- asked for UTF-8\n");
    errno = 0;
    printf("setlocale %d errno %d\n", setlocale(LC_CTYPE, ".UTF-8") != 0, errno);
    all(65001);
    refused("bad\xFF.txt");
    refused("bad\xC3");
    printf("raw: Gr\xC3\xBC\xC3\x9F" "e, \xCE\xB1\xCE\xB2\xCE\xB3, \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\n");

    printf("-- every spelling\n");
    const char *const names[] = { ".UTF-8", ".UTF8", ".utf8", ".utf-8", ".Utf-8", ".65001" };
    for (unsigned k = 0; k < sizeof names / sizeof names[0]; k++) {
        setlocale(LC_ALL, "C");
        printf("%s %d MB_CUR_MAX %d\n", names[k], setlocale(LC_CTYPE, names[k]) != 0, (int)MB_CUR_MAX);
    }
    printf("-- given back\n");
    char saved[128];
    strcpy(saved, setlocale(LC_ALL, 0));
    setlocale(LC_ALL, "C");
    printf("C MB_CUR_MAX %d\n", (int)MB_CUR_MAX);
    printf("restored %d MB_CUR_MAX %d\n", setlocale(LC_ALL, saved) != 0, (int)MB_CUR_MAX);

    printf("-- not known\n");
    printf("bogus %d MB_CUR_MAX %d\n", setlocale(LC_CTYPE, "bogus") != 0, (int)MB_CUR_MAX);
    printf("C.UTF-8 %d MB_CUR_MAX %d\n", setlocale(LC_CTYPE, "C.UTF-8") != 0, (int)MB_CUR_MAX);

    printf("-- back to C\n");
    printf("setlocale %d\n", setlocale(LC_ALL, "C") != 0);
    printf("ctype %s all %s\n", setlocale(LC_CTYPE, 0), setlocale(LC_ALL, 0));
    write(ansi, "ansi\n");
    printf("windows has the ANSI name %d\n", windowsHas(cafe));
    readBack(ansi);
    remove(ansi);
    all(0);
    return 0;
}
