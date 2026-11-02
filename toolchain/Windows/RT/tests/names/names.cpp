// names.cpp - the C library names of stage 2 that cpp11's headers declare (docs\Runtime_Design.md 16): llabs, ldiv,
// lldiv, atoll; mblen, mbtowc, wctomb, mbstowcs, wcstombs in the "C" locale; isblank, strncat, strcoll, strxfrm;
// setvbuf, setbuf, fgetpos, fsetpos, _fseeki64, _ftelli64, tmpnam, tmpfile; vsscanf, vfscanf. Run by probe.ps1
// against the UCRT's transcript (names.expected).
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern "C" int _stat64i32(const char *, void *);
extern "C" long long _ftelli64(FILE *);
extern "C" int _fseeki64(FILE *, long long, int);
extern "C" unsigned long GetTempPathW(unsigned long, wchar_t *);
extern "C" int WideCharToMultiByte(unsigned, unsigned long, const wchar_t *, int, char *, int, const char *, int *);

static long sizeOf(const char *path)
{
    // st_size of struct _stat64i32 is the 32-bit word at offset 20
    unsigned char st[64];
    if (_stat64i32(path, st) != 0) return -1;
    return *(long *)(st + 20);
}

static int scanFrom(const char *text, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vsscanf(text, format, ap);
    va_end(ap);
    return n;
}

static int scanFile(FILE *f, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vfscanf(f, format, ap);
    va_end(ap);
    return n;
}

static void numbers()
{
    printf("llabs %lld %lld %lld\n", llabs(-5), llabs(LLONG_MAX), llabs(0));
    ldiv_t l = ldiv(-7, 2);
    lldiv_t ll = lldiv(-7000000000000LL, 3);
    printf("ldiv %ld %ld lldiv %lld %lld\n", l.quot, l.rem, ll.quot, ll.rem);
    const char *texts[] = { "42", "  -17x", "+9223372036854775807", "9223372036854775808", "-9223372036854775809", "", "abc", " 0x10" };
    for (int i = 0; i < 8; i++) {
        errno = 0;
        long long v = atoll(texts[i]);
        printf("atoll \"%s\" %lld errno %d\n", texts[i], v, errno);
    }
}

static void multibyte()
{
    wchar_t w = 0x1234;
    errno = 0;
    printf("mblen null %d, \"a\" %d, \"\" %d, n0 %d, \\xe9 %d\n", mblen(0, 1), mblen("a", 1), mblen("", 1), mblen("a", 0),
           mblen("\xe9", 1));
    int r = mbtowc(&w, "b", 1);
    printf("mbtowc b %d %x\n", r, (unsigned)w);
    r = mbtowc(&w, "\xe9", 1);
    printf("mbtowc e9 %d %x\n", r, (unsigned)w);
    w = 0x1234;
    r = mbtowc(&w, "", 1);
    printf("mbtowc empty %d %x\n", r, (unsigned)w);
    w = 0x1234;
    r = mbtowc(&w, "c", 0);
    printf("mbtowc n0 %d %x, null %d, no pwc %d\n", r, (unsigned)w, mbtowc(&w, 0, 1), mbtowc(0, "d", 1));
    char out[8] = { 'x', 'x', 'x', 'x', 0 };
    errno = 0;
    r = wctomb(out, L'z');
    printf("wctomb z %d %c, null %d\n", r, out[0], wctomb(0, L'z'));
    r = wctomb(out, (wchar_t)0xe9);
    printf("wctomb e9 %d %02x errno %d\n", r, (unsigned char)out[0], errno);
    errno = 0;
    r = wctomb(out, (wchar_t)0x100);
    printf("wctomb 100 %d errno %d\n", r, errno);
    wchar_t wide[8];
    size_t n = mbstowcs(wide, "hi\xe9", 8);
    printf("mbstowcs %d %x %x %x %x\n", (int)n, (unsigned)wide[0], (unsigned)wide[1], (unsigned)wide[2], (unsigned)wide[3]);
    printf("mbstowcs count %d, short %d\n", (int)mbstowcs(0, "four", 0), (int)mbstowcs(wide, "longer", 3));
    char narrow[8];
    const wchar_t ok[] = { 'o', 'k', 0xe9, 0 };
    n = wcstombs(narrow, ok, 8);
    printf("wcstombs %d %02x %02x %02x\n", (int)n, (unsigned char)narrow[0], (unsigned char)narrow[1], (unsigned char)narrow[2]);
    const wchar_t bad[] = { 'a', 0x263a, 0 };
    errno = 0;
    n = wcstombs(narrow, bad, 8);
    printf("wcstombs bad %d errno %d, count %d\n", (int)n, errno, (int)wcstombs(0, ok, 0));
}

static void strings()
{
    int blanks = 0, first = -1, value = 0;
    for (int c = -1; c < 256; c++)
        if (isblank(c)) { blanks++; if (first < 0) first = c; value = isblank(c); }
    printf("isblank %d first %d value %d tab %d\n", blanks, first, value, isblank('\t'));
    char buf[16] = "ab";
    strncat(buf, "cdef", 2);
    printf("strncat %s", buf);
    strncat(buf, "gh", 9);
    printf(" %s\n", buf);
    printf("strcoll %d %d %d\n", strcoll("abc", "abd") < 0, strcoll("b", "a") > 0, strcoll("same", "same"));
    char x[8];
    memset(x, '#', sizeof x);
    size_t n = strxfrm(x, "text", sizeof x);
    printf("strxfrm %d %s, count %d\n", (int)n, x, (int)strxfrm(0, "longer text", 0));
}

static void buffering(const char *dir)
{
    char path[300];
    strcpy(path, dir);
    strcat(path, "grt-names-buf.txt");
    FILE *f = fopen(path, "wb");
    int r = setvbuf(f, 0, _IOFBF, 16);
    fwrite("0123456789abcdefghij", 1, 20, f);
    printf("setvbuf 16: %d, on disk %ld", r, sizeOf(path));
    fflush(f);
    printf(" then %ld\n", sizeOf(path));
    fclose(f);
    f = fopen(path, "wb");
    char mine[7];
    r = setvbuf(f, mine, _IOLBF, 7);
    fputs("line\n", f);
    fputs("ab", f);
    printf("setvbuf line 7: %d, on disk %ld", r, sizeOf(path));
    fputs("cdefgh", f);
    printf(" then %ld\n", sizeOf(path));
    fclose(f);
    f = fopen(path, "wb");
    setbuf(f, 0);
    fputs("seven!!", f);
    printf("setbuf null: on disk %ld\n", sizeOf(path));
    fclose(f);
    f = fopen(path, "wb");
    r = setvbuf(f, 0, _IONBF, 0);
    fputc('a', f);
    printf("setvbuf none: %d, on disk %ld\n", r, sizeOf(path));
    fclose(f);

    f = fopen(path, "w+b");
    fputs("0123456789", f);
    fpos_t pos;
    r = fgetpos(f, &pos);
    printf("fgetpos %d %lld", r, (long long)pos);
    fseek(f, 3, SEEK_SET);
    printf(" getc %c", getc(f));
    r = fsetpos(f, &pos);
    printf(" fsetpos %d tell %ld", r, ftell(f));
    pos = 4;
    fsetpos(f, &pos);
    printf(" getc %c\n", getc(f));
    r = _fseeki64(f, 6, SEEK_SET);
    printf("_fseeki64 %d _ftelli64 %lld getc %c", r, _ftelli64(f), getc(f));
    r = _fseeki64(f, -2, SEEK_END);
    printf(" end-2 %d %lld", r, _ftelli64(f));
    errno = 0;
    r = _fseeki64(f, -100, SEEK_SET);
    printf(" before start %d errno %d\n", r, errno);
    fclose(f);
    remove(path);

    f = fopen(path, "w+");
    fputs("12 apples 3.5\nnext", f);
    rewind(f);
    int count = 0;
    char word[16];
    double d = 0;
    r = scanFile(f, "%d %15s %lf", &count, word, &d);
    printf("vfscanf %d %d %s %.1f\n", r, count, word, d);
    fclose(f);
    remove(path);
    int a = 0, b = 0;
    r = scanFrom("7,8", "%d,%d", &a, &b);
    printf("vsscanf %d %d %d, empty %d\n", r, a, b, scanFrom("", "%d", &a));
}

static void temporary(const char *dir)
{
    char mine[L_tmpnam];
    char *a = tmpnam(0);
    char first[L_tmpnam];
    strcpy(first, a);
    char *b = tmpnam(mine);
    char *c = tmpnam(0);
    size_t dirLength = strlen(dir);
    printf("tmpnam: own buffer %d, same static %d, differ %d %d, in temp %d %d, exists %ld\n", b == mine, a == c,
           strcmp(first, mine) != 0, strcmp(mine, c) != 0, strncmp(first, dir, dirLength) == 0,
           strncmp(mine, dir, dirLength) == 0, sizeOf(mine));
    FILE *t = tmpfile();
    printf("tmpfile %d", t != 0);
    fputs("scratch", t);
    rewind(t);
    char back[8] = { 0 };
    fread(back, 1, 7, t);
    printf(" %s\n", back);
    fclose(t);
}

int main()
{
    wchar_t wide[300];
    char dir[300];
    GetTempPathW(300, wide);
    WideCharToMultiByte(0, 0, wide, -1, dir, sizeof dir, 0, 0);
    numbers();
    multibyte();
    strings();
    buffering(dir);
    temporary(dir);
    return 0;
}
