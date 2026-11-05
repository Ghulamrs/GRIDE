// Spec: ISO C 7.24.2 and 7.24.3 (wchar.h: wide formatted and character I/O), as the Microsoft runtime has them on
// a stream opened as every grt stream is, without a Unicode mode (tests\wide\wide.expected):
// - In text mode a wide character is the byte of its value, in the "C" locale: fputwc of one past 0xFF answers WEOF
//   with errno EILSEQ, and the wide printf family writes '?' for it. Once setlocale asks for UTF-8 it is written as
//   UTF-8, and only a surrogate has no bytes, which writes nothing. In binary mode it is two bytes, low first.
// - fgetwc reads one byte in text mode, CR LF and CTRL+Z as narrow reading takes them, and two in binary mode;
//   ungetwc gives back the same, in UTF-8 too. fputws answers 0, or EOF.
// - The wprintf family reads a wide format with the runtime's legacy meanings (Printer.h): %s and %c wide, %S and
//   %C narrow. swprintf and vswprintf write at most n code units with the NUL and answer -1 for a text that does not
//   fit, written as far as it goes.
// - The wscanf family reads with the same meanings, by RTS6x's scanner over a wide source: the format's
//   conversions are given their lengths (%s as %ls, %S as %s ...). As there, white space and digits are ASCII's;
//   the runtime's also take other scripts', and a format's own characters past 0xFF match nothing here.
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#include "fmt/Printer.h"
#include "stdio/InputSource.h"
#include "stdio/Scanner.h"
#include "StreamWin.h"

namespace {

bool textMode(FILE *f)
{
    int info = __grt_descriptor(f->fd);
    return info >= 0 && (info & GRT_TEXT) != 0;
}

// fputwc's work, the stream locked
wint_t putWide(wchar_t c, FILE *f)
{
    if (textMode(f)) {
        char b[3];
        int n = grt::narrowUnit(c, b);
        if (n < 0) {
            errno = EILSEQ;
            return __grt_utf8 ? c : WEOF;       // the Microsoft runtime's: in UTF-8 nothing written, and no failure said
        }
        return grt::put(f, b, (unsigned)n) < 0 ? WEOF : c;
    }
    char two[2] = { (char)(c & 0xFF), (char)(c >> 8) };
    return grt::put(f, two, 2) < 0 ? WEOF : c;
}

// The format of a wide scanf call as RTS6x's scanner reads it: each code unit a byte (past 0xFF, 0xFF), and each
// conversion's length made plain - %s %c %[ wide (l), %S %C narrow, w as l. In buf when it fits, else from malloc.
char *narrowFormat(const wchar_t *format, char *buf, size_t size, char **heap)
{
    size_t n = wcslen(format);
    char *to = buf;
    *heap = 0;
    if (2 * n + 1 > size) {
        *heap = (char *)malloc(2 * n + 1);
        if (!*heap) __grt_fail_fast();
        to = *heap;
    }
    size_t k = 0;
    const wchar_t *p = format;
    while (*p) {
        wchar_t c = *p++;
        to[k++] = (char)(c > 0xFF ? 0xFF : c);
        if (c != L'%') continue;
        if (*p == L'%') { to[k++] = '%'; p++; continue; }
        if (*p == L'*') to[k++] = (char)*p++;
        while (*p >= L'0' && *p <= L'9') to[k++] = (char)*p++;
        bool sized = false, narrow = false;
        if (*p == L'w') { to[k++] = 'l'; p++; sized = true; }
        else if (*p == L'l' || *p == L'h' || *p == L'L' || *p == L'j' || *p == L'z' || *p == L't' || *p == L'q') {
            narrow = *p == L'h';
            while (*p == L'l' || *p == L'h' || *p == L'L' || *p == L'j' || *p == L'z' || *p == L't' || *p == L'q')
                to[k++] = (char)*p++;
            sized = true;
        }
        wchar_t conversion = *p;
        if (!conversion) break;
        p++;
        if (conversion == L'S' || conversion == L'C') {
            to[k++] = (char)(conversion == L'S' ? 's' : 'c');
            continue;
        }
        if (!sized && !narrow && (conversion == L's' || conversion == L'c' || conversion == L'[')) to[k++] = 'l';
        to[k++] = (char)conversion;
        if (conversion == L'[') {
            if (*p == L'^') to[k++] = (char)*p++;
            if (*p == L']') to[k++] = (char)*p++;
            while (*p && *p != L']') { wchar_t s = *p++; to[k++] = (char)(s > 0xFF ? 0xFF : s); }
            if (*p == L']') to[k++] = (char)*p++;
        }
    }
    to[k] = 0;
    return to;
}

}  // namespace

// The wide character the scanner reads next from a stream, locked by the caller (InputSource.cpp), or EOF
extern "C" int __grt_get_wide(FILE *f)
{
    rts6x::Stream in(f);
    int lo = in.get();
    if (lo == EOF || textMode(f)) return lo;
    int hi = in.get();
    return hi == EOF ? EOF : lo | (hi << 8);
}

extern "C" void __grt_unget_wide(int c, FILE *f)
{
    rts6x::Stream in(f);
    if (textMode(f)) {
        in.unget(c);
        return;
    }
    in.unget((c >> 8) & 0xFF);
    in.unget(c & 0xFF);
}

extern "C" wint_t fputwc(wchar_t c, FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    wint_t r = putWide(c, stream);
    grt::unlock(stream, held);
    return r;
}

extern "C" wint_t putwc(wchar_t c, FILE *stream)
{
    return fputwc(c, stream);
}

extern "C" wint_t putwchar(wchar_t c)
{
    return fputwc(c, &_ftable[1]);
}

extern "C" int fputws(const wchar_t *s, FILE *stream)
{
    if (!s) __grt_fail_fast();
    grt::valid(stream);
    bool held = grt::lock(stream);
    int r = 0;
    for (; *s; s++)
        if (putWide(*s, stream) == WEOF) { r = EOF; break; }
    grt::unlock(stream, held);
    return r;
}

extern "C" wint_t fgetwc(FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    int c = __grt_get_wide(stream);
    grt::unlock(stream, held);
    return c == EOF ? WEOF : (wint_t)c;
}

extern "C" wint_t getwc(FILE *stream)
{
    return fgetwc(stream);
}

extern "C" wint_t getwchar(void)
{
    return fgetwc(&_ftable[0]);
}

extern "C" wint_t ungetwc(wint_t c, FILE *stream)
{
    grt::valid(stream);
    if (c == WEOF) return WEOF;
    bool held = grt::lock(stream);
    wint_t r = c;
    if (textMode(stream)) {
        if (c > 0xFF || rts6x::Stream(stream).unget((int)c) == EOF) r = WEOF;
    } else {
        rts6x::Stream in(stream);
        if (in.unget((int)(c >> 8)) == EOF || in.unget((int)(c & 0xFF)) == EOF) r = WEOF;
    }
    grt::unlock(stream, held);
    return r;
}

extern "C" wchar_t *fgetws(wchar_t *s, int n, FILE *stream)
{
    grt::valid(stream);
    if (!s || n <= 0) __grt_fail_fast();
    bool held = grt::lock(stream);
    int i = 0;
    while (i < n - 1) {
        int c = __grt_get_wide(stream);
        if (c == EOF) break;
        s[i++] = (wchar_t)c;
        if (c == L'\n') break;
    }
    grt::unlock(stream, held);
    if (i == 0 && n > 1) return 0;
    s[i] = 0;
    return s;
}

extern "C" int vfwprintf(FILE *stream, const wchar_t *format, va_list args)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    int n = grt::wprintToStream(stream, format, &args);
    grt::unlock(stream, held);
    return n;
}

extern "C" int fwprintf(FILE *stream, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vfwprintf(stream, format, args);
    va_end(args);
    return n;
}

extern "C" int vwprintf(const wchar_t *format, va_list args)
{
    return vfwprintf(&_ftable[1], format, args);
}

extern "C" int wprintf(const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vfwprintf(&_ftable[1], format, args);
    va_end(args);
    return n;
}

extern "C" int vswprintf(wchar_t *s, size_t n, const wchar_t *format, va_list args)
{
    if (!s && n != 0) __grt_fail_fast();
    return grt::wprintToMemory(s, n, format, &args);
}

extern "C" int swprintf(wchar_t *s, size_t n, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    int r = vswprintf(s, n, format, args);
    va_end(args);
    return r;
}

extern "C" int vswscanf(const wchar_t *s, const wchar_t *format, va_list args)
{
    if (!s || !format) __grt_fail_fast();
    char buf[256], *heap;
    const char *f = narrowFormat(format, buf, sizeof buf, &heap);
    rts6x::InputSource in(s);
    int n = rts6x::Scanner(in, &args).run(f);
    free(heap);
    return n;
}

extern "C" int swscanf(const wchar_t *s, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vswscanf(s, format, args);
    va_end(args);
    return n;
}

extern "C" int vfwscanf(FILE *stream, const wchar_t *format, va_list args)
{
    grt::valid(stream);
    if (!format) __grt_fail_fast();
    char buf[256], *heap;
    const char *f = narrowFormat(format, buf, sizeof buf, &heap);
    bool held = grt::lock(stream);
    rts6x::InputSource in(stream, true);
    int n = rts6x::Scanner(in, &args).run(f);
    grt::unlock(stream, held);
    free(heap);
    return n;
}

extern "C" int fwscanf(FILE *stream, const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vfwscanf(stream, format, args);
    va_end(args);
    return n;
}

extern "C" int vwscanf(const wchar_t *format, va_list args)
{
    return vfwscanf(&_ftable[0], format, args);
}

extern "C" int wscanf(const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vfwscanf(&_ftable[0], format, args);
    va_end(args);
    return n;
}
