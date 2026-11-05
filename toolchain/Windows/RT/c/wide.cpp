// Spec: ISO C 7.24 (wchar.h: wide strings and their conversions) and 7.8 (inttypes.h), as the Microsoft runtime has
// them in its "C" locale (its transcript: tests\wide\wide.expected):
// - The string functions work on 16-bit code units. wcsxfrm copies as wcsncpy does, zeros after, and answers the
//   source's length; wcscoll compares as wcscmp; wcstok is C's, with its third argument.
// - wcstol and the other wide conversions read what strtol and the others read: the white space iswspace sees,
//   and besides ASCII's the digits of the scripts the runtime takes (Arabic-Indic ... Mongolian, and fullwidth).
// The multibyte conversions are c\multibyte.cpp.
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uchar.h>
#include <wchar.h>
#include <wctype.h>
#include "grt.h"

namespace {

// a code unit's value as a digit of the scripts the Microsoft runtime reads, or -1
int scriptDigit(unsigned c)
{
    static const unsigned short zeros[] = { 0x0030, 0x0660, 0x06F0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66, 0x0C66,
                                            0x0CE6, 0x0D66, 0x0E50, 0x0ED0, 0x0F20, 0x1040, 0x17E0, 0x1810, 0xFF10 };
    for (unsigned i = 0; i < sizeof zeros / sizeof zeros[0]; i++)
        if (c >= zeros[i] && c < zeros[i] + 10u) return (int)(c - zeros[i]);
    return -1;
}

// The wide text at s as the narrow conversions read it, a byte for each code unit up to the first that no number
// takes: a digit its ASCII digit, white space a space, ASCII itself. In buf when it fits, else from malloc (*heap).
char *narrowed(const wchar_t *s, char *buf, size_t size, char **heap)
{
    size_t n = 0;
    for (;; n++) {
        unsigned c = s[n];
        if (c == 0 || (c >= 0x80 && scriptDigit(c) < 0 && !iswspace((wint_t)c))) break;
    }
    *heap = 0;
    char *to = buf;
    if (n >= size) {
        *heap = (char *)malloc(n + 1);
        if (!*heap) n = size - 1;
        else to = *heap;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned c = s[i];
        int d = scriptDigit(c);
        to[i] = d >= 0 ? (char)('0' + d) : c < 0x80 ? (char)c : ' ';
    }
    to[n] = 0;
    return to;
}

void finish(const wchar_t *s, wchar_t **end, const char *text, const char *stop, char *heap)
{
    if (end) *end = (wchar_t *)s + (grt::bytesBetween(text, stop));
    free(heap);
}

const size_t Room = 128;
}  // namespace

extern "C" size_t wcslen(const wchar_t *s)
{
    const wchar_t *p = s;
    while (*p) p++;
    return grt::bytesBetween(s, p) >> 1;
}

extern "C" wchar_t *wcscpy(wchar_t *to, const wchar_t *from)
{
    wchar_t *p = to;
    while ((*p++ = *from++) != 0) {}
    return to;
}

extern "C" wchar_t *wcsncpy(wchar_t *to, const wchar_t *from, size_t n)
{
    size_t i = 0;
    for (; i < n && from[i]; i++) to[i] = from[i];
    for (; i < n; i++) to[i] = 0;
    return to;
}

extern "C" wchar_t *wcscat(wchar_t *to, const wchar_t *from)
{
    wcscpy(to + wcslen(to), from);
    return to;
}

extern "C" wchar_t *wcsncat(wchar_t *to, const wchar_t *from, size_t n)
{
    wchar_t *end = to + wcslen(to);
    while (n-- && *from) *end++ = *from++;
    *end = 0;
    return to;
}

extern "C" int wcscmp(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a < *b ? -1 : *a > *b;
}

extern "C" int wcsncmp(const wchar_t *a, const wchar_t *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return *a < *b ? -1 : 1;
        if (!*a) break;
    }
    return 0;
}

extern "C" int wcscoll(const wchar_t *a, const wchar_t *b)
{
    return wcscmp(a, b);
}

extern "C" size_t wcsxfrm(wchar_t *to, const wchar_t *from, size_t n)
{
    if (n) wcsncpy(to, from, n);
    return wcslen(from);
}

extern "C" wchar_t *wcschr(const wchar_t *s, wchar_t c)
{
    for (;; s++) {
        if (*s == c) return (wchar_t *)s;
        if (!*s) return 0;
    }
}

extern "C" wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    const wchar_t *last = 0;
    for (;; s++) {
        if (*s == c) last = s;
        if (!*s) return (wchar_t *)last;
    }
}

extern "C" wchar_t *wcsstr(const wchar_t *s, const wchar_t *part)
{
    if (!*part) return (wchar_t *)s;
    for (; *s; s++) {
        size_t i = 0;
        while (part[i] && s[i] == part[i]) i++;
        if (!part[i]) return (wchar_t *)s;
    }
    return 0;
}

extern "C" size_t wcsspn(const wchar_t *s, const wchar_t *set)
{
    size_t n = 0;
    while (s[n] && wcschr(set, s[n])) n++;
    return n;
}

extern "C" size_t wcscspn(const wchar_t *s, const wchar_t *set)
{
    size_t n = 0;
    while (s[n] && !wcschr(set, s[n])) n++;
    return n;
}

extern "C" wchar_t *wcspbrk(const wchar_t *s, const wchar_t *set)
{
    s += wcscspn(s, set);
    return *s ? (wchar_t *)s : 0;
}

extern "C" wchar_t *wcstok(wchar_t *s, const wchar_t *delimiters, wchar_t **state)
{
    if (!s) s = *state;
    if (!s) return 0;
    s += wcsspn(s, delimiters);
    if (!*s) { *state = 0; return 0; }
    wchar_t *end = s + wcscspn(s, delimiters);
    if (*end) *end++ = 0;
    *state = end;
    return s;
}

extern "C" long wcstol(const wchar_t *s, wchar_t **end, int base)
{
    char buf[Room], *heap, *stop;
    char *text = narrowed(s, buf, Room, &heap);
    long r = strtol(text, &stop, base);
    finish(s, end, text, stop, heap);
    return r;
}

extern "C" long long wcstoll(const wchar_t *s, wchar_t **end, int base)
{
    char buf[Room], *heap, *stop;
    char *text = narrowed(s, buf, Room, &heap);
    long long r = strtoll(text, &stop, base);
    finish(s, end, text, stop, heap);
    return r;
}

extern "C" unsigned long wcstoul(const wchar_t *s, wchar_t **end, int base)
{
    char buf[Room], *heap, *stop;
    char *text = narrowed(s, buf, Room, &heap);
    unsigned long r = strtoul(text, &stop, base);
    finish(s, end, text, stop, heap);
    return r;
}

extern "C" unsigned long long wcstoull(const wchar_t *s, wchar_t **end, int base)
{
    char buf[Room], *heap, *stop;
    char *text = narrowed(s, buf, Room, &heap);
    unsigned long long r = strtoull(text, &stop, base);
    finish(s, end, text, stop, heap);
    return r;
}

extern "C" double wcstod(const wchar_t *s, wchar_t **end)
{
    char buf[Room], *heap, *stop;
    char *text = narrowed(s, buf, Room, &heap);
    double r = strtod(text, &stop);
    finish(s, end, text, stop, heap);
    return r;
}

extern "C" float wcstof(const wchar_t *s, wchar_t **end)
{
    char buf[Room], *heap, *stop;
    char *text = narrowed(s, buf, Room, &heap);
    float r = strtof(text, &stop);
    finish(s, end, text, stop, heap);
    return r;
}

extern "C" long double wcstold(const wchar_t *s, wchar_t **end)
{
    char buf[Room], *heap, *stop;
    char *text = narrowed(s, buf, Room, &heap);
    long double r = strtold(text, &stop);
    finish(s, end, text, stop, heap);
    return r;
}

extern "C" intmax_t imaxabs(intmax_t j)
{
    return j < 0 ? -j : j;
}

extern "C" imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom)
{
    imaxdiv_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

extern "C" intmax_t strtoimax(const char *s, char **end, int base)
{
    return strtoll(s, end, base);
}

extern "C" uintmax_t strtoumax(const char *s, char **end, int base)
{
    return strtoull(s, end, base);
}

extern "C" intmax_t wcstoimax(const wchar_t *s, wchar_t **end, int base)
{
    return wcstoll(s, end, base);
}

extern "C" uintmax_t wcstoumax(const wchar_t *s, wchar_t **end, int base)
{
    return wcstoull(s, end, base);
}

// wcsftime: each conversion strftime's, widened; the rest of the format as it is
extern "C" size_t wcsftime(wchar_t *s, size_t size, const wchar_t *format, const struct tm *t)
{
    size_t n = 0;
    while (*format) {
        if (*format != L'%') {
            if (n + 1 >= size) return 0;
            s[n++] = *format++;
            continue;
        }
        char spec[8];
        int k = 0;
        spec[k++] = '%';
        format++;
        while (k < 6 && (*format == L'#' || *format == L'E' || *format == L'O')) spec[k++] = (char)*format++;
        if (!*format) break;
        spec[k++] = *format < 0x80 ? (char)*format : '?';
        format++;
        spec[k] = 0;
        char text[256];
        size_t m = strftime(text, sizeof text, spec, t);
        for (size_t i = 0; i < m; i++) {
            if (n + 1 >= size) return 0;
            s[n++] = (wchar_t)(unsigned char)text[i];
        }
    }
    if (n >= size) return 0;
    s[n] = 0;
    return n;
}
