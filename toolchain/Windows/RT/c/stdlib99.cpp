// Spec: ISO C99 7.20 - llabs, ldiv, lldiv, atoll; 7.20.7 and 7.20.8 - mblen, mbtowc, wctomb, mbstowcs, wcstombs in
// the "C" locale, as the Microsoft runtime's: a byte is the wide character of the same value (0..255), no encoding
// has a shift state, and a wide character above 0xFF converts to nothing: -1, errno EILSEQ.
#include <errno.h>
#include <stdlib.h>

extern "C" long long llabs(long long v)
{
    return v < 0 ? -v : v;
}

extern "C" ldiv_t ldiv(long numerator, long denominator)
{
    ldiv_t r;
    r.quot = numerator / denominator;
    r.rem = numerator % denominator;
    return r;
}

extern "C" lldiv_t lldiv(long long numerator, long long denominator)
{
    lldiv_t r;
    r.quot = numerator / denominator;
    r.rem = numerator % denominator;
    return r;
}

extern "C" long long atoll(const char *s)
{
    return strtoll(s, 0, 10);
}

extern "C" int mblen(const char *s, size_t n)
{
    if (!s || n == 0 || !*s) return 0;
    return 1;
}

extern "C" int mbtowc(wchar_t *wide, const char *s, size_t n)
{
    if (!s || n == 0) return 0;
    if (wide) *wide = (wchar_t)(unsigned char)*s;
    return *s ? 1 : 0;
}

extern "C" int wctomb(char *s, wchar_t wide)
{
    if (!s) return 0;
    if ((unsigned)wide > 0xFF) {
        errno = EILSEQ;
        return -1;
    }
    *s = (char)wide;
    return 1;
}

extern "C" size_t mbstowcs(wchar_t *wide, const char *s, size_t n)
{
    size_t i = 0;
    if (!wide) {
        while (s[i]) i++;
        return i;
    }
    for (; i < n; i++) {
        wide[i] = (wchar_t)(unsigned char)s[i];
        if (!s[i]) break;
    }
    return i;
}

extern "C" size_t wcstombs(char *s, const wchar_t *wide, size_t n)
{
    size_t i = 0;
    for (; !s || i < n; i++) {
        if ((unsigned)wide[i] > 0xFF) {
            errno = EILSEQ;
            return (size_t)-1;
        }
        if (s) s[i] = (char)wide[i];
        if (!wide[i]) break;
    }
    return i;
}
