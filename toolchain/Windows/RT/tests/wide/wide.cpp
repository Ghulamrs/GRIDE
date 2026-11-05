// wide.cpp - the names cpp11 1.53's C headers declare beyond 1.44's: wide strings and their conversions, wctype,
// uchar, inttypes, fenv, wide streams with their printf and scanf, rand_s. Run on the UCRT, its transcript is
// wide.expected; on GRIDE's runtime it must print the same. wctype's answers for every code unit are a count and
// a hash each.
#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>

// as <random> declares it
extern "C" int rand_s(unsigned int *value);

static void units(const char *what, const wchar_t *s, int n)
{
    printf("%s:", what);
    for (int i = 0; i < n; i++) printf(" %04x", (unsigned)s[i]);
    printf("\n");
}

static void bytes(const char *what, const unsigned char *s, int n)
{
    printf("%s:", what);
    for (int i = 0; i < n; i++) printf(" %02x", s[i]);
    printf("\n");
}

static unsigned long long fnv(unsigned long long h, unsigned v) { return (h ^ v) * 1099511628211ull; }

static void strings()
{
    wchar_t a[32], b[32];
    printf("wcslen %u %u\n", (unsigned)wcslen(L"hello"), (unsigned)wcslen(L""));
    wmemset(a, 0x7777, 32);
    wcscpy(a, L"abc");
    units("wcscpy", a, 5);
    wmemset(a, 0x7777, 32);
    wcsncpy(a, L"ab", 5);
    units("wcsncpy pad", a, 7);
    wcsncpy(a, L"abcdef", 3);
    units("wcsncpy cut", a, 7);
    wcscpy(a, L"one");
    wcscat(a, L"two");
    wcsncat(a, L"three", 2);
    units("wcscat wcsncat", a, 9);
    const wchar_t high[] = { 0xff00, 0 };
    printf("wcscmp %d %d %d %d\n", wcscmp(L"abc", L"abd") < 0, wcscmp(L"b", L"a") > 0, wcscmp(L"x", L"x"), wcscmp(high, L"a") > 0);
    printf("wcsncmp %d %d\n", wcsncmp(L"abcx", L"abcy", 3), wcsncmp(L"abcx", L"abcy", 4) < 0);
    printf("wcscoll %d %d\n", wcscoll(L"abc", L"abd") < 0, wcscoll(L"abc", L"abc"));
    wmemset(b, 0x7777, 32);
    printf("wcsxfrm %u %u\n", (unsigned)wcsxfrm(b, L"xyz", 10), (unsigned)wcsxfrm(0, L"hello", 0));
    units("wcsxfrm", b, 5);
    const wchar_t *s = L"hello, world";
    printf("wcschr %d %d\n", (int)(wcschr(s, L'o') - s), wcschr(s, L'q') == 0);
    printf("wcschr nul %d\n", (int)(wcschr(s, 0) - s));
    printf("wcsrchr %d\n", (int)(wcsrchr(s, L'o') - s));
    printf("wcsstr %d %d %d\n", (int)(wcsstr(s, L"wor") - s), wcsstr(s, L"xyz") == 0, (int)(wcsstr(s, L"") - s));
    printf("wcspbrk %d %d\n", (int)(wcspbrk(s, L" ,") - s), wcspbrk(s, L"qz") == 0);
    printf("wcsspn %u wcscspn %u\n", (unsigned)wcsspn(s, L"hel"), (unsigned)wcscspn(s, L" ,"));
    wchar_t t[] = L"  a,b;;c  ";
    wchar_t *state = 0;
    for (wchar_t *w = wcstok(t, L" ,;", &state); w; w = wcstok(0, L" ,;", &state)) units("wcstok", w, (int)wcslen(w));
    printf("wmemchr %d wmemcmp %d\n", (int)(wmemchr(s, L'w', 12) - s), wmemcmp(L"ab", L"ac", 2) < 0);
}

static void numbers()
{
    // cpp11 makes a \x escape in a wide literal one byte and reads the literal as UTF-8, so these are spelt out
    const wchar_t arabic[] = { 0x0661, 0x0662, 0 }, fullwidth[] = { 0xff11, 0xff12, 0 }, thai[] = { 0x3000, 0x0e51, L'7', 0 };
    const wchar_t *cases[] = { L"42", L"  -17xyz", L"0x1F", L"077", L"zz", L"9223372036854775808",
                               L"-9223372036854775809", L"18446744073709551616", arabic, fullwidth, L"+", L"", thai };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        wchar_t *end;
        errno = 0;
        long l = wcstol(cases[i], &end, 0);
        int e1 = errno; errno = 0;
        long long ll = wcstoll(cases[i], 0, 10);
        int e2 = errno; errno = 0;
        unsigned long ul = wcstoul(cases[i], 0, 16);
        int e3 = errno; errno = 0;
        unsigned long long ull = wcstoull(cases[i], 0, 36);
        int e4 = errno;
        printf("wcsto* %u: %ld end %d e%d | %lld e%d | %lu e%d | %llu e%d\n", i, l, (int)(end - cases[i]), e1, ll, e2, ul, e3, ull, e4);
    }
    const wchar_t arabicReal[] = { 0x0661, L'.', 0x0665, 0 };
    const wchar_t *reals[] = { L"1.5", L"-0x1p3", L"  2.5e-3xyz", L"inf", L"-nan", L"1e400", L"1e-400", L".", L"0x", arabicReal };
    for (unsigned i = 0; i < sizeof reals / sizeof reals[0]; i++) {
        wchar_t *end;
        errno = 0;
        double d = wcstod(reals[i], &end);
        int e1 = errno; errno = 0;
        float f = wcstof(reals[i], 0);
        int e2 = errno; errno = 0;
        long double ld = wcstold(reals[i], 0);
        printf("wcstod %u: %.17g end %d e%d | %.9g e%d | %.17Lg\n", i, d, (int)(end - reals[i]), e1, (double)f, e2, ld);
    }
    imaxdiv_t q = imaxdiv(-17, 5);
    printf("imaxabs %lld imaxdiv %lld %lld\n", (long long)imaxabs(-5), (long long)q.quot, (long long)q.rem);
    char *end;
    errno = 0;
    intmax_t a = strtoimax("-9223372036854775808", &end, 10);
    int e1 = errno; errno = 0;
    intmax_t b = strtoimax("9223372036854775808", 0, 10);
    int e2 = errno; errno = 0;
    uintmax_t c = strtoumax("18446744073709551615", 0, 10);
    int e3 = errno; errno = 0;
    uintmax_t d = strtoumax("-1", 0, 10);
    int e4 = errno; errno = 0;
    intmax_t w = wcstoimax(L"  0x7fffffffffffffff", 0, 0);
    uintmax_t x = wcstoumax(L"777", 0, 8);
    printf("strtoimax %lld e%d %lld e%d strtoumax %llu e%d %llu e%d wcstoimax %lld wcstoumax %llu\n", (long long)a, e1,
           (long long)b, e2, (unsigned long long)c, e3, (unsigned long long)d, e4, (long long)w, (unsigned long long)x);
}

static void classes()
{
    int (*is[])(wint_t) = { iswalnum, iswalpha, iswblank, iswcntrl, iswdigit, iswgraph, iswlower, iswprint, iswpunct,
                            iswspace, iswupper, iswxdigit };
    const char *names[] = { "alnum", "alpha", "blank", "cntrl", "digit", "graph", "lower", "print", "punct", "space",
                            "upper", "xdigit" };
    for (int k = 0; k < 12; k++) {
        unsigned n = 0, n256 = 0;
        unsigned long long h = 1469598103934665603ull;
        for (unsigned c = 0; c < 0x10000; c++) {
            int r = is[k]((wint_t)c) != 0;
            n += r;
            if (c < 256) n256 += r;
            h = fnv(h, (unsigned)r);
        }
        wctype_t t = wctype(names[k]);
        unsigned same = 0;
        for (unsigned c = 0; c < 0x10000; c++) same += (iswctype((wint_t)c, t) != 0) == (is[k]((wint_t)c) != 0);
        printf("isw%s %u (%u below 256) %016llx wctype %04x iswctype agrees %u\n", names[k], n, n256, h, (unsigned)t, same);
    }
    printf("wctype bad %u iswctype 0 %d\n", (unsigned)wctype("vowel"), iswctype(L'a', 0));
    wint_t (*to[])(wint_t) = { towlower, towupper };
    for (int k = 0; k < 2; k++) {
        unsigned n = 0;
        unsigned long long h = 1469598103934665603ull;
        for (unsigned c = 0; c < 0x10000; c++) {
            wint_t r = to[k]((wint_t)c);
            n += r != c;
            h = fnv(h, r);
        }
        printf("tow%s changes %u %016llx; WEOF %04x\n", k ? "upper" : "lower", n, h, (unsigned)to[k](WEOF));
    }
    wctrans_t lo = wctrans("tolower"), up = wctrans("toupper");
    printf("wctrans %04x %04x bad %04x towctrans %04x %04x %04x\n", (unsigned)lo, (unsigned)up, (unsigned)wctrans("rot13"),
           (unsigned)towctrans(L'A', lo), (unsigned)towctrans(L'a', up), (unsigned)towctrans(L'a', 0));
}

static void multibyte()
{
    unsigned long long h = 1469598103934665603ull;
    for (int c = 0; c < 256; c++) h = fnv(h, btowc(c));
    printf("btowc %016llx EOF %04x\n", h, (unsigned)btowc(EOF));
    h = 1469598103934665603ull;
    for (unsigned c = 0; c < 0x200; c++) h = fnv(h, (unsigned)wctob((wint_t)c));
    printf("wctob %016llx WEOF %d\n", h, wctob(WEOF));
    mbstate_t st;
    memset(&st, 0, sizeof st);
    wchar_t wc = 0x7777;
    const char *ins[] = { "A", "\xe9", "", "\x80" };
    for (int i = 0; i < 4; i++) {
        wc = 0x7777;
        errno = 0;
        size_t r = mbrtowc(&wc, ins[i], 4, &st);
        printf("mbrtowc %d: %d %04x e%d mbrlen %d\n", i, (int)r, (unsigned)wc, errno, (int)mbrlen(ins[i], 4, &st));
    }
    printf("mbrtowc n0 %d null s %d\n", (int)mbrtowc(&wc, "A", 0, &st), (int)mbrtowc(0, 0, 0, &st));
    char out[8];
    memset(out, 0x55, sizeof out);
    wchar_t ws[] = { L'A', 0xe9, 0x100, 0 };
    for (int i = 0; i < 3; i++) {
        errno = 0;
        memset(out, 0x55, sizeof out);
        size_t r = wcrtomb(out, ws[i], &st);
        printf("wcrtomb %04x: %d %02x e%d\n", (unsigned)ws[i], (int)r, (unsigned char)out[0], errno);
    }
    printf("wcrtomb null %d\n", (int)wcrtomb(0, L'A', &st));
    const char *src = "abc\xe9";
    wchar_t dst[8];
    wmemset(dst, 0x7777, 8);
    size_t r = mbsrtowcs(dst, &src, 8, &st);
    printf("mbsrtowcs %d src %s\n", (int)r, src ? "set" : "null");
    units("mbsrtowcs", dst, 6);
    src = "abcdef";
    wmemset(dst, 0x7777, 8);
    r = mbsrtowcs(dst, &src, 3, &st);
    printf("mbsrtowcs 3: %d rest %s\n", (int)r, src);
    src = "abcdef";
    printf("mbsrtowcs count %d\n", (int)mbsrtowcs(0, &src, 0, &st));
    const wchar_t xy[] = { L'x', L'y', 0xe9, 0 };
    const wchar_t *wsrc = xy;
    memset(out, 0x55, sizeof out);
    r = wcsrtombs(out, &wsrc, 8, &st);
    printf("wcsrtombs %d src %s\n", (int)r, wsrc ? "set" : "null");
    bytes("wcsrtombs", (const unsigned char *)out, 5);
    const wchar_t bad[] = { L'x', 0x263a, 0 };
    wsrc = bad;
    errno = 0;
    r = wcsrtombs(out, &wsrc, 8, &st);
    printf("wcsrtombs bad %d e%d at %d\n", (int)r, errno, (int)(wsrc - bad));
}

static void utf()
{
    mbstate_t st;
    memset(&st, 0, sizeof st);
    const char *ins[] = { "A", "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xff", "\xe2\x82" };
    for (int i = 0; i < 6; i++) {
        char16_t c16 = 0x7777;
        char32_t c32 = 0x7777;
        memset(&st, 0, sizeof st);
        errno = 0;
        size_t r = mbrtoc16(&c16, ins[i], strlen(ins[i]), &st);
        int e = errno;
        printf("mbrtoc16 %d: %d %04x e%d", i, (int)r, (unsigned)c16, e);
        if (i == 3) { size_t r2 = mbrtoc16(&c16, ins[i] + 4, 0, &st); printf(" then %d %04x", (int)r2, (unsigned)c16); }
        memset(&st, 0, sizeof st);
        errno = 0;
        r = mbrtoc32(&c32, ins[i], strlen(ins[i]), &st);
        printf(" | mbrtoc32 %d %08x e%d\n", (int)r, (unsigned)c32, errno);
    }
    const char16_t c16s[] = { u'A', 0xe9, 0x20ac, 0xd83d, 0xde00, 0xde00 };
    memset(&st, 0, sizeof st);
    for (int i = 0; i < 6; i++) {
        char out[8];
        memset(out, 0x55, sizeof out);
        errno = 0;
        size_t r = c16rtomb(out, c16s[i], &st);
        printf("c16rtomb %04x: %d e%d", (unsigned)c16s[i], (int)r, errno);
        bytes("", (const unsigned char *)out, r > 0 && r < 8 ? (int)r : 0);
    }
    const char32_t c32s[] = { U'A', 0xe9, 0x1f600, 0x110000, 0xd800 };
    for (int i = 0; i < 5; i++) {
        char out[8];
        memset(out, 0x55, sizeof out);
        memset(&st, 0, sizeof st);
        errno = 0;
        size_t r = c32rtomb(out, c32s[i], &st);
        printf("c32rtomb %08x: %d e%d", (unsigned)c32s[i], (int)r, errno);
        bytes("", (const unsigned char *)out, r > 0 && r < 8 ? (int)r : 0);
    }
}

static volatile double vzero = 0.0, vone = 1.0, vthree = 3.0, vbig = DBL_MAX, vtiny = DBL_MIN;

static void environment()
{
    printf("fegetround %x\n", (unsigned)fegetround());
    const int modes[] = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO, 0x1234 };
    for (int i = 0; i < 5; i++) {
        int r = fesetround(modes[i]);
        volatile double third = vone / vthree, minus = -vone / vthree;
        printf("fesetround %x: %d now %x third %a %a\n", (unsigned)modes[i], r, (unsigned)fegetround(), third, minus);
    }
    fesetround(FE_TONEAREST);
    printf("feclearexcept %d test %x\n", feclearexcept(FE_ALL_EXCEPT), (unsigned)fetestexcept(FE_ALL_EXCEPT));
    volatile double v;
    v = vzero / vzero; printf("0/0 %x\n", (unsigned)fetestexcept(FE_ALL_EXCEPT)); feclearexcept(FE_ALL_EXCEPT);
    v = vone / vzero; printf("1/0 %x\n", (unsigned)fetestexcept(FE_ALL_EXCEPT)); feclearexcept(FE_ALL_EXCEPT);
    v = vbig * vthree; printf("overflow %x\n", (unsigned)fetestexcept(FE_ALL_EXCEPT)); feclearexcept(FE_ALL_EXCEPT);
    v = vtiny / vthree; printf("underflow %x\n", (unsigned)fetestexcept(FE_ALL_EXCEPT)); feclearexcept(FE_ALL_EXCEPT);
    v = vone / vthree; printf("inexact %x only %x\n", (unsigned)fetestexcept(FE_ALL_EXCEPT), (unsigned)fetestexcept(FE_INEXACT | FE_INVALID));
    (void)v;
    fexcept_t flags = 0x5555;
    printf("fegetexceptflag %d %lx\n", fegetexceptflag(&flags, FE_ALL_EXCEPT), (unsigned long)flags);
    feclearexcept(FE_ALL_EXCEPT);
    printf("fesetexceptflag %d test %x\n", fesetexceptflag(&flags, FE_INEXACT), (unsigned)fetestexcept(FE_ALL_EXCEPT));
    printf("feraiseexcept %d test %x\n", feraiseexcept(FE_OVERFLOW | FE_DIVBYZERO), (unsigned)fetestexcept(FE_ALL_EXCEPT));
    fenv_t env;
    printf("fegetenv %d ctl %08lx stat %08lx\n", fegetenv(&env), env._Fe_ctl, env._Fe_stat);
    fenv_t held;
    printf("feholdexcept %d ctl %08lx stat %08lx test %x\n", feholdexcept(&held), held._Fe_ctl, held._Fe_stat,
           (unsigned)fetestexcept(FE_ALL_EXCEPT));
    fesetround(FE_UPWARD);
    fenv_t up;
    fegetenv(&up);
    printf("upward ctl %08lx\n", up._Fe_ctl);
    printf("fesetenv %d round %x test %x\n", fesetenv(&env), (unsigned)fegetround(), (unsigned)fetestexcept(FE_ALL_EXCEPT));
    v = vone / vzero;
    printf("feupdateenv %d test %x\n", feupdateenv(&held), (unsigned)fetestexcept(FE_ALL_EXCEPT));
    printf("FE_DFL_ENV %d round %x test %x\n", fesetenv(FE_DFL_ENV), (unsigned)fegetround(), (unsigned)fetestexcept(FE_ALL_EXCEPT));
    feclearexcept(FE_ALL_EXCEPT);
}

static int vsw(wchar_t *buf, size_t n, const wchar_t *format, ...)
{
    va_list a;
    va_start(a, format);
    int r = vswprintf(buf, n, format, a);
    va_end(a);
    return r;
}

static int vsws(const wchar_t *in, const wchar_t *format, ...)
{
    va_list a;
    va_start(a, format);
    int r = vswscanf(in, format, a);
    va_end(a);
    return r;
}

static int vfw(FILE *f, const wchar_t *format, ...)
{
    va_list a;
    va_start(a, format);
    int r = vfwprintf(f, format, a);
    va_end(a);
    return r;
}

static void dump(const char *name, const char *mode)
{
    FILE *f = fopen(name, "rb");
    unsigned char b[200];
    int n = (int)fread(b, 1, sizeof b, f);
    fclose(f);
    printf("file %s %d bytes\n", mode, n);
    bytes("  bytes", b, n);
}

static void streams()
{
    const char *modes[] = { "w", "wb" };
    for (int m = 0; m < 2; m++) {
        FILE *f = fopen("wide.txt", modes[m]);
        printf("fputwc %04x", (unsigned)fputwc(L'A', f));
        printf(" %04x", (unsigned)fputwc(L'\n', f));
        printf(" fputws %d", fputws(L"two\n", f));
        printf(" fwprintf %d", fwprintf(f, L"%d %s %S %c %C|%5.2f|%ls|%hs|%x\n", 42, L"wide", "narrow", L'w', 'n', 3.14159, L"ls", "hs", 255));
        printf(" vfwprintf %d", vfw(f, L"[%3d]\n", 7));
        errno = 0;
        wint_t r = fputwc(0x100, f);
        printf(" fputwc 0100 %04x e%d", (unsigned)r, errno);
        errno = 0;
        int k = fwprintf(f, L"x%cy", 0x263a);
        printf(" fwprintf 263a %d e%d", k, errno);
        printf(" putwc %04x\n", (unsigned)putwc(L'Z', f));
        fclose(f);
        dump("wide.txt", modes[m]);
        f = fopen("wide.txt", m ? "rb" : "r");
        wint_t c1 = fgetwc(f), c2 = getwc(f);
        printf("fgetwc %04x %04x ungetwc %04x", (unsigned)c1, (unsigned)c2, (unsigned)ungetwc(L'Q', f));
        printf(" again %04x", (unsigned)fgetwc(f));
        wchar_t line[8];
        wmemset(line, 0x7777, 8);
        wchar_t *got = fgetws(line, 5, f);
        printf(" fgetws %d\n", got == line);
        units("  line", line, 6);
        while (fgetwc(f) != WEOF) {}
        printf("end feof %d fgetwc %04x\n", feof(f) != 0, (unsigned)fgetwc(f));
        fclose(f);
    }
    remove("wide.txt");
    wchar_t buf[64];
    wmemset(buf, 0x7777, 64);
    int r = swprintf(buf, 64, L"%d|%s|%S|%c|%C|%x|%-6d|%06.2f|%e|%ls|%hs|%%", -5, L"ws", "ns", L'c', 'C', 0xbeef, 12, 2.5, 1e10, L"l", "h");
    printf("swprintf %d\n", r);
    units("  buf", buf, r > 0 ? r + 1 : 4);
    wmemset(buf, 0x7777, 64);
    r = swprintf(buf, 4, L"%s", L"toolong");
    printf("swprintf short %d\n", r);
    units("  buf", buf, 5);
    wmemset(buf, 0x7777, 64);
    r = swprintf(buf, 4, L"abc");
    printf("swprintf exact %d\n", r);
    units("  buf", buf, 5);
    r = vsw(buf, 64, L"%5s|%-5ls|%.2s", L"ab", L"cd", L"efgh");
    printf("vswprintf %d\n", r);
    units("  buf", buf, r > 0 ? r : 0);
    r = swprintf(buf, 64, L"[%c]", 0x263a);
    printf("swprintf 263a %d\n", r);
    units("  buf", buf, r > 0 ? r : 0);
    int i = 0, n = 0;
    double d = 0;
    wchar_t w1[16], w3[16];
    char s2[16];
    wmemset(w1, 0x7777, 16);
    memset(s2, 0x55, 16);
    r = swscanf(L"  42 abc 3.5 xyz rest", L"%d %s %lf %S%n", &i, w1, &d, s2, &n);
    printf("swscanf %d: %d %.2f n %d\n", r, i, d, n);
    units("  w1", w1, 4);
    bytes("  s2", (const unsigned char *)s2, 4);
    wchar_t c = 0x7777;
    char nc = 0x55;
    r = swscanf(L"q r", L"%c %C", &c, &nc);
    printf("swscanf c %d: %04x %02x\n", r, (unsigned)c, (unsigned char)nc);
    wmemset(w3, 0x7777, 16);
    r = swscanf(L"abc123", L"%[a-z]%d", w3, &i);
    printf("swscanf set %d: %d\n", r, i);
    units("  w3", w3, 4);
    r = vsws(L"7 8", L"%d %d", &i, &n);
    printf("vswscanf %d: %d %d\n", r, i, n);
    printf("swscanf empty %d\n", swscanf(L"", L"%d", &i));
    r = wprintf(L"wprintf %d %s\n", 7, L"seven");
    printf("wprintf %d\n", r);
    putwchar(L'P');
    putwchar(L'\n');
    fflush(stdout);
}

int main()
{
    strings();
    numbers();
    classes();
    multibyte();
    utf();
    environment();
    streams();
    unsigned int a = 1, b = 1;
    int r1 = rand_s(&a), r2 = rand_s(&b);
    printf("rand_s %d %d differ %d\n", r1, r2, a != b);
    return 0;
}
