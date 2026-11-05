// wchar.h - [c.strings] Table 74: wide characters, their strings, conversions and I/O.
//
// wchar_t is the target's: 16 bits on Windows and the C6747, 32 on glibc and Darwin, and wint_t, WEOF and
// mbstate_t follow each C library. The UCRT defines fwide, mbsinit and the wmem functions in its header and
// exports none of them, so on Windows they are inline here as they are there; its printf and scanf family
// are legacy_stdio_definitions.lib's, the CRT linked with every program here.
#ifndef _CXX1_WCHAR_H
#define _CXX1_WCHAR_H

#include <_gride.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
typedef unsigned short wint_t;
typedef struct _Mbstatet { unsigned long _Wchar; unsigned short _Byte, _State; } mbstate_t;
#define WEOF ((wint_t)0xFFFF)
#define WCHAR_MIN 0x0000
#define WCHAR_MAX 0xffff
#elif defined(__APPLE__)
typedef int wint_t;
typedef union { char __mbstate8[128]; long long _mbstateL; } mbstate_t;
#define WEOF ((wint_t)-1)
#define WCHAR_MIN (-0x7fffffff - 1)
#define WCHAR_MAX 0x7fffffff
#elif defined(__TMS320C6X__)
typedef unsigned int wint_t;
typedef int mbstate_t;
#define WEOF ((wint_t)-1)
#define WCHAR_MIN 0
#define WCHAR_MAX 0xffff
#else
typedef unsigned int wint_t;
typedef struct { int __count; union { unsigned int __wch; char __wchb[4]; } __value; } mbstate_t;
#define WEOF (0xffffffffu)
#define WCHAR_MIN (-0x7fffffff - 1)
#define WCHAR_MAX 0x7fffffff
#endif

wint_t btowc(int c);
int wctob(wint_t c);
size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
size_t mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps);
size_t wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps);

wint_t fgetwc(FILE *stream);
wchar_t *fgetws(wchar_t *s, int n, FILE *stream);
wint_t fputwc(wchar_t c, FILE *stream);
int fputws(const wchar_t *s, FILE *stream);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t putwc(wchar_t c, FILE *stream);
wint_t putwchar(wchar_t c);
wint_t ungetwc(wint_t c, FILE *stream);
int fwprintf(FILE *stream, const wchar_t *format, ...);
int fwscanf(FILE *stream, const wchar_t *format, ...);
int swprintf(wchar_t *s, size_t n, const wchar_t *format, ...);
int swscanf(const wchar_t *s, const wchar_t *format, ...);
int vfwprintf(FILE *stream, const wchar_t *format, va_list arg);
int vfwscanf(FILE *stream, const wchar_t *format, va_list arg);
int vswprintf(wchar_t *s, size_t n, const wchar_t *format, va_list arg);
int vswscanf(const wchar_t *s, const wchar_t *format, va_list arg);
int vwprintf(const wchar_t *format, va_list arg);
int vwscanf(const wchar_t *format, va_list arg);
int wprintf(const wchar_t *format, ...);
int wscanf(const wchar_t *format, ...);

double wcstod(const wchar_t *nptr, wchar_t **endptr);
float wcstof(const wchar_t *nptr, wchar_t **endptr);
long double wcstold(const wchar_t *nptr, wchar_t **endptr);
long wcstol(const wchar_t *nptr, wchar_t **endptr, int base);
long long wcstoll(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long wcstoul(const wchar_t *nptr, wchar_t **endptr, int base);
unsigned long long wcstoull(const wchar_t *nptr, wchar_t **endptr, int base);

wchar_t *wcscpy(wchar_t *s1, const wchar_t *s2);
wchar_t *wcsncpy(wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wcscat(wchar_t *s1, const wchar_t *s2);
wchar_t *wcsncat(wchar_t *s1, const wchar_t *s2, size_t n);
int wcscmp(const wchar_t *s1, const wchar_t *s2);
int wcscoll(const wchar_t *s1, const wchar_t *s2);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
size_t wcsxfrm(wchar_t *s1, const wchar_t *s2, size_t n);
size_t wcscspn(const wchar_t *s1, const wchar_t *s2);
size_t wcslen(const wchar_t *s);
size_t wcsspn(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcstok(wchar_t *s1, const wchar_t *s2, wchar_t **ptr);
size_t wcsftime(wchar_t *s, size_t maxsize, const wchar_t *format, const struct tm *timeptr);
// C's signatures, as lib/string.h gives strchr: [c.strings]/4's const pairs are not here yet.
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcspbrk(const wchar_t *s1, const wchar_t *s2);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *s1, const wchar_t *s2);

#if defined(_WIN32)
_GRIDE_INLINE int fwide(FILE *stream, int mode) { (void)stream; return mode; }
_GRIDE_INLINE int mbsinit(const mbstate_t *ps) { return ps == 0 || ps->_Wchar == 0; }
_GRIDE_INLINE wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n) {
    for (; n != 0; ++s, --n)
        if (*s == c) return (wchar_t *)s;
    return 0;
}
_GRIDE_INLINE int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n) {
    for (; n != 0; ++s1, ++s2, --n)
        if (*s1 != *s2) return *s1 < *s2 ? -1 : 1;
    return 0;
}
_GRIDE_INLINE wchar_t *wmemcpy(wchar_t *s1, const wchar_t *s2, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) s1[i] = s2[i];
    return s1;
}
_GRIDE_INLINE wchar_t *wmemmove(wchar_t *s1, const wchar_t *s2, size_t n) {
    size_t i;
    if (s1 < s2) for (i = 0; i < n; i++) s1[i] = s2[i];
    else for (i = n; i != 0; i--) s1[i - 1] = s2[i - 1];
    return s1;
}
_GRIDE_INLINE wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) s[i] = c;
    return s;
}
#else
int fwide(FILE *stream, int mode);
int mbsinit(const mbstate_t *ps);
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n);
int wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemcpy(wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemmove(wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
