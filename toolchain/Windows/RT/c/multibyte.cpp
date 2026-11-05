// Spec: ISO C 7.20.7, 7.20.8, 7.24.6 and C11 7.28.1 - the multibyte functions, one member so that a program using none
// of them links none of it. As the Microsoft runtime has them (transcripts: tests\wide, tests\utf8):
// - MB_CUR_MAX (7.20/3, ___mb_cur_max_func behind it) is the most bytes a character takes: 1, or 4 in UTF-8.
// - In the "C" locale a character is one byte, a byte is the wide character of the same value (0..255), no encoding
//   has a shift state, and a wide character above 0xFF has no byte: -1, errno EILSEQ.
// - mbrtoc16, c16rtomb, mbrtoc32 and c32rtomb read and write UTF-8 whatever the locale: a code point past one UTF-16
//   unit comes as two, the second answered -3; a surrogate, an overlong form or a code point past 0x10FFFF is EILSEQ.
// - Once setlocale asks for UTF-8 (__grt_utf8) the rest read and write it too: mblen and mbtowc answer -1 for an
//   incomplete or illegal character, mbtowc and mbrtowc U+FFFD for one past a single code unit; wctomb and wcrtomb
//   refuse a surrogate; the string functions take a pair as one character, stopping before one that does not fit;
//   btowc and wctob take ASCII alone.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <wchar.h>
#include "grt.h"

namespace {

const size_t Incomplete = (size_t)-2, Illegal = (size_t)-1;

mbstate_t wideState, wideOutState;

size_t utf8ToWide(wchar_t *to, const char **from, size_t n, mbstate_t *state)
{
    const char *s = *from;
    size_t count = 0;
    while (!to || count < n) {
        size_t left = !s[0] ? 1 : !s[1] ? 2 : !s[2] ? 3 : 4;    // a character ends before the NUL does
        char32_t c32 = 0;
        size_t r = mbrtoc32(&c32, s, left, state);
        if (r > 4) {
            if (to) *from = s;
            return Illegal;
        }
        if (r == 0) {
            if (to) {
                to[count] = 0;
                *from = 0;
            }
            return count;
        }
        if (c32 > 0xFFFF) {
            if (to) {
                if (n - count < 2) break;
                to[count] = (wchar_t)(0xD800 | ((c32 - 0x10000) >> 10));
                to[count + 1] = (wchar_t)(0xDC00 | ((c32 - 0x10000) & 0x3FF));
            }
            count += 2;
        } else {
            if (to) to[count] = (wchar_t)c32;
            count++;
        }
        s += r;
    }
    *from = s;
    return count;
}

size_t wideToUtf8(char *to, const wchar_t **from, size_t n, mbstate_t *state)
{
    const wchar_t *p = *from, *start = p;       // start: the unit the character being read began at
    char spare[4];
    size_t count = 0;
    for (;;) {
        char *at = to && n - count >= 4 ? to + count : spare;
        size_t r = c16rtomb(at, (char16_t)*p, state);
        if (r == Illegal) {
            if (to) *from = start;
            return Illegal;
        }
        if (to && at == spare) {
            if (n - count < r) {
                *from = start;
                return count;
            }
            memcpy(to + count, spare, r);
        }
        if (r > 0 && at[r - 1] == 0) {
            if (to) *from = 0;
            return count + r - 1;
        }
        p++;
        if (r > 0) start = p;
        count += r;
    }
}

}  // namespace

extern "C" int ___mb_cur_max_func(void)
{
    return __grt_utf8 ? 4 : 1;
}

extern "C" int mblen(const char *s, size_t n)
{
    static mbstate_t state;
    if (!s || n == 0 || !*s) {
        memset(&state, 0, sizeof state);
        return 0;
    }
    if (!__grt_utf8) return 1;
    size_t r = mbrtoc32(0, s, n, &state);
    return r > 4 ? -1 : (int)r;
}

extern "C" int mbtowc(wchar_t *wide, const char *s, size_t n)
{
    static mbstate_t state;
    if (!s || n == 0) {
        memset(&state, 0, sizeof state);
        return 0;
    }
    if (__grt_utf8 && *s) {
        char32_t c32 = 0;
        size_t r = mbrtoc32(&c32, s, n, &state);
        if (r > 4) return -1;
        if (wide) *wide = (wchar_t)(c32 > 0xFFFF ? 0xFFFD : c32);
        return (int)r;
    }
    if (wide) *wide = (wchar_t)(unsigned char)*s;
    return *s ? 1 : 0;
}

extern "C" int wctomb(char *s, wchar_t wide)
{
    if (!s) return 0;
    if (__grt_utf8) {
        mbstate_t own;
        memset(&own, 0, sizeof own);
        size_t r = c32rtomb(s, (char32_t)wide, &own);
        return r > 4 ? -1 : (int)r;
    }
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
    if (__grt_utf8) {
        mbstate_t own;
        memset(&own, 0, sizeof own);
        return mbsrtowcs(wide, &s, n, &own);
    }
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
    if (__grt_utf8) {
        mbstate_t own;
        memset(&own, 0, sizeof own);
        return wcsrtombs(s, &wide, n, &own);
    }
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

extern "C" wint_t btowc(int c)
{
    if (__grt_utf8 && (unsigned char)c >= 0x80) return WEOF;
    return c == EOF ? WEOF : (wint_t)(unsigned char)c;
}

// the byte as a signed char, as the runtime answers it: 0x80 to 0xFF are -128 to -1
extern "C" int wctob(wint_t c)
{
    if (__grt_utf8) return c < 0x80 ? (int)c : EOF;
    return c < 0x100 ? (int)(signed char)c : EOF;
}

extern "C" size_t mbrtowc(wchar_t *to, const char *s, size_t n, mbstate_t *state)
{
    if (to) *to = 0;
    if (!s || n == 0 || !*s) return 0;
    if (__grt_utf8) {
        char32_t c32 = 0;
        size_t r = mbrtoc32(&c32, s, n, state ? state : &wideState);
        if (r <= 4 && to) *to = (wchar_t)(c32 > 0xFFFF ? 0xFFFD : c32);
        return r;
    }
    if (to) *to = (wchar_t)(unsigned char)*s;
    return 1;
}

extern "C" size_t mbrlen(const char *s, size_t n, mbstate_t *state)
{
    return mbrtowc(0, s, n, state);
}

extern "C" size_t wcrtomb(char *s, wchar_t c, mbstate_t *state)
{
    if (!s) return 1;
    if (__grt_utf8) {
        mbstate_t own;
        memset(&own, 0, sizeof own);
        return c32rtomb(s, c, state ? state : &own);
    }
    if (c > 0xFF) {
        errno = EILSEQ;
        return (size_t)-1;
    }
    *s = (char)c;
    return 1;
}

extern "C" size_t mbsrtowcs(wchar_t *to, const char **from, size_t n, mbstate_t *state)
{
    if (__grt_utf8) return utf8ToWide(to, from, n, state ? state : &wideState);
    const unsigned char *p = (const unsigned char *)*from;
    if (!to) return strlen((const char *)p);
    size_t i = 0;
    for (; i < n; i++, p++) {
        to[i] = *p;
        if (!*p) { *from = 0; return i; }
    }
    *from = (const char *)p;
    return i;
}

extern "C" size_t wcsrtombs(char *to, const wchar_t **from, size_t n, mbstate_t *state)
{
    if (__grt_utf8) return wideToUtf8(to, from, n, state ? state : &wideOutState);
    const wchar_t *p = *from;
    size_t i = 0;
    for (; !to || i < n; i++, p++) {
        if (*p > 0xFF) {
            if (to) *from = p;
            errno = EILSEQ;
            return (size_t)-1;
        }
        if (to) to[i] = (char)*p;
        if (!*p) {
            if (to) *from = 0;
            return i;
        }
    }
    *from = p;
    return i;
}

namespace {

size_t illegal(mbstate_t *state)
{
    memset(state, 0, sizeof *state);
    errno = EILSEQ;
    return Illegal;
}

size_t reset(size_t answer, mbstate_t *state)
{
    memset(state, 0, sizeof *state);
    return answer;
}

// one UTF-8 sequence's length by its first byte: 0 for NUL, 1 for ASCII, 2 to 4, or -1 for no sequence's
int utf8Length(unsigned char b)
{
    if (b < 0x80) return b ? 1 : 0;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return -1;
}

mbstate_t utf16State, utf16OutState, utf32State, utf32OutState;

}  // namespace

// The state: _Wchar the bits so far, _Byte the sequence's length, _State the bytes still to come
extern "C" size_t mbrtoc32(char32_t *to, const char *s, size_t n, mbstate_t *state)
{
    if (!state) state = &utf32State;
    if (!s) {
        s = "";
        n = 1;
        to = 0;
    }
    if (n == 0) return Incomplete;
    const unsigned char *p = (const unsigned char *)s;
    unsigned c32, length, needed;
    size_t read = 0;
    if (state->_State == 0) {
        int k = utf8Length(*p);
        unsigned char first = *p++;
        read = 1;
        if (k == 0 || k == 1) {
            if (to) *to = first;
            return (size_t)k;
        }
        if (k < 0) return illegal(state);
        length = needed = (unsigned)k;
        c32 = first & ((1u << (7 - k)) - 1);
    } else {
        c32 = state->_Wchar;
        length = state->_Byte;
        needed = state->_State;
        if (length < 2 || length > 4 || needed < 1 || needed >= length) return illegal(state);
    }
    if (needed < n) n = needed;
    for (; read < n; read++) {
        unsigned char b = *p++;
        if ((b & 0xC0) != 0x80) return illegal(state);
        c32 = (c32 << 6) | (b & 0x3Fu);
    }
    if (n < needed) {
        state->_Wchar = c32;
        state->_Byte = (unsigned short)length;
        state->_State = (unsigned short)(needed - n);
        return Incomplete;
    }
    static const unsigned least[3] = { 0x80, 0x800, 0x10000 };
    if ((c32 >= 0xD800 && c32 <= 0xDFFF) || c32 > 0x10FFFF || c32 < least[length - 2]) return illegal(state);
    if (to) *to = c32;
    return reset(c32 ? needed : 0, state);
}

// The state after a code point past one unit: _State 0xFFFF, _Wchar the code point for its second unit
extern "C" size_t mbrtoc16(char16_t *to, const char *s, size_t n, mbstate_t *state)
{
    if (!state) state = &utf16State;
    if (state->_State == 0xFFFF) {
        if (to) *to = (char16_t)(((state->_Wchar - 0x10000) & 0x3FF) | 0xDC00);
        return reset((size_t)-3, state);
    }
    char32_t c32 = 0;
    size_t r = mbrtoc32(&c32, s, n, state);
    if (!s || r == Illegal || r == Incomplete) return r;
    if (c32 <= 0xFFFF) {
        if (to) *to = (char16_t)c32;
        return reset(r, state);
    }
    state->_Wchar = c32;
    state->_State = 0xFFFF;
    if (to) *to = (char16_t)((((c32 - 0x10000) & 0xFFFFF) >> 10) | 0xD800);
    return r;
}

extern "C" size_t c32rtomb(char *s, char32_t c32, mbstate_t *state)
{
    if (!state) state = &utf32OutState;
    if (!s) return reset(1, state);
    if (c32 == 0) {
        *s = 0;
        return reset(1, state);
    }
    if (c32 < 0x80) {
        *s = (char)c32;
        return 1;
    }
    unsigned trail;
    unsigned char lead;
    if (c32 < 0x800) { trail = 1; lead = 0xC0; }
    else if (c32 < 0x10000) {
        if (c32 >= 0xD800 && c32 <= 0xDFFF) return illegal(state);
        trail = 2; lead = 0xE0;
    } else if (c32 <= 0x10FFFF) { trail = 3; lead = 0xF0; }
    else return illegal(state);
    for (unsigned i = trail; i > 0; i--) {
        s[i] = (char)((c32 & 0x3F) | 0x80);
        c32 >>= 6;
    }
    s[0] = (char)(c32 | lead);
    return reset(trail + 1, state);
}

// The state after a first surrogate: _Wchar its code point's high bits
extern "C" size_t c16rtomb(char *s, char16_t c16, mbstate_t *state)
{
    if (!state) state = &utf16OutState;
    if (state->_Wchar == 0) {
        if (c16 >= 0xDC00 && c16 <= 0xDFFF) return illegal(state);
        if (c16 >= 0xD800 && c16 <= 0xDBFF) {
            state->_Wchar = ((unsigned long)(c16 & ~0xD800) << 10) + 0x10000;
            return 0;
        }
        return c32rtomb(s, c16, state);
    }
    if (c16 < 0xDC00 || c16 > 0xDFFF) return illegal(state);
    char32_t c32 = (char32_t)(state->_Wchar + (c16 & ~0xDC00));
    mbstate_t own;
    memset(&own, 0, sizeof own);
    return reset(c32rtomb(s, c32, &own), state);
}
