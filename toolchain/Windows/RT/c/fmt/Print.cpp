// Print.cpp - what the printf engine (Printer.h) shares: the format's table of states, integer digits, the float
// digits (the common values' exactly here, the rest from RTS6x's formatter), and the two ends, a stream and memory.
#include <stdlib.h>
#include "Printer.h"
#include "stdio/Formatter.h"
#include "stdio/OutputSink.h"
#include "../StreamWin.h"

namespace grt {

int narrowUnit(unsigned c, char *to)
{
    if (!__grt_utf8 || c < 0x80) {
        if (c > 0xFF) return -1;
        to[0] = (char)c;
        return 1;
    }
    if (c >= 0xD800 && c <= 0xDFFF) return -1;
    if (c < 0x800) {
        to[0] = (char)(0xC0 | (c >> 6));
        to[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    to[0] = (char)(0xE0 | (c >> 12));
    to[1] = (char)(0x80 | ((c >> 6) & 0x3F));
    to[2] = (char)(0x80 | (c & 0x3F));
    return 3;
}

int narrowCharacter(const unsigned short *s, int n, int *at, char *to)
{
    unsigned c = s[*at];
    int k = narrowUnit(c, to);
    if (k >= 0 || !__grt_utf8 || c > 0xDBFF) return k;
    if (++*at == n) return 0;
    if (s[*at] < 0xDC00 || s[*at] > 0xDFFF) return -1;
    c = 0x10000 + ((c - 0xD800) << 10) + (s[*at] - 0xDC00u);
    to[0] = (char)(0xF0 | (c >> 18));
    to[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    to[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    to[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

namespace {

const unsigned long long tens[20] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull, 100000000ull, 1000000000ull,
    10000000000ull, 100000000000ull, 1000000000000ull, 10000000000000ull, 100000000000000ull,
    1000000000000000ull, 10000000000000000ull, 100000000000000000ull, 1000000000000000000ull,
    10000000000000000000ull
};

// m x 10^k x 2^e, m below 2^53 and k from 0 to 19, computed exactly in 128 bits: its whole part, false when that
// needs more than 64 bits, and 1 in up when the rest rounds it up - above a half, or a half with the whole part odd
// (to nearest, ties to even, as RTS6x's DecimalDigits rounds the exact digits)
bool scaled(unsigned long long m, int k, int e, unsigned long long &whole, unsigned &up)
{
    unsigned long long t = tens[k];
    unsigned long long m0 = m & 0xFFFFFFFFull, m1 = m >> 32, t0 = t & 0xFFFFFFFFull, t1 = t >> 32;
    unsigned long long p00 = m0 * t0, p01 = m0 * t1, p10 = m1 * t0, p11 = m1 * t1;
    unsigned long long mid = (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);
    unsigned long long lo = (mid << 32) | (p00 & 0xFFFFFFFFull);
    unsigned long long hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    up = 0;
    if (e >= 0) {
        if (e >= 64 || hi != 0 || (e > 0 && (lo >> (64 - e)) != 0)) return false;
        whole = lo << e;
        return true;
    }
    unsigned s = (unsigned)-e;
    if (s >= 128) {                 // the product is below 2^117: the rest is below a half
        whole = 0;
        return true;
    }
    if (s < 64) {
        if ((hi >> s) != 0) return false;
        whole = (hi << (64 - s)) | (lo >> s);
        unsigned long long r = lo & ((1ull << s) - 1), h = 1ull << (s - 1);
        up = r > h || (r == h && (whole & 1) != 0) ? 1 : 0;
    } else if (s == 64) {
        whole = hi;
        unsigned long long h = 1ull << 63;
        up = lo > h || (lo == h && (whole & 1) != 0) ? 1 : 0;
    } else {
        unsigned u = s - 64;
        whole = hi >> u;
        unsigned long long r = hi & ((1ull << u) - 1), h = 1ull << (u - 1);
        up = r > h || (r == h && (lo != 0 || (whole & 1) != 0)) ? 1 : 0;
    }
    return true;
}

// v = m x 2^e, normal, rounded to precision + 1 significant digits (precision at most 17): q with
// 10^precision <= q < 10^(precision+1) and the decimal exponent x of the rounded value; false where the scale
// 10^(precision - x) is not one of tens
bool significant(unsigned long long m, int e, int precision, int &x, unsigned long long &q)
{
    int b = e + 52;                 // 2^b <= v < 2^(b+1); 78913 / 2^18 is a little below log10(2)
    x = b >= 0 ? (b * 78913) >> 18 : -((-b * 78913 + 262143) >> 18);
    for (int tries = 0; tries < 4; tries++) {
        int k = precision - x;
        unsigned long long whole;
        unsigned up;
        if (k < 0 || k > 19 || !scaled(m, k, e, whole, up)) return false;
        if (whole < tens[precision]) { x--; continue; }
        if (whole >= tens[precision + 1]) { x++; continue; }
        q = whole + up;
        if (q == tens[precision + 1]) { q = tens[precision]; x++; }
        return true;
    }
    return false;
}

// q's digits, at least n of them (zeros before), backward from end: their start
char *padded(unsigned long long q, int n, char *end)
{
    char *d = decimalDigits(q, end);
    while ((int)bytesBetween(d, end) < n) *--d = '0';
    return d;
}

}  // namespace

char *decimalDigits(unsigned long long n, char *end)
{
    static const char pairs[] =
        "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
        "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";
    char *d = end;
    // cpp11 divides by a constant with div: one per pair above 2^32, and below it a multiply by the reciprocal,
    // (m * 1374389535) >> 37 being m / 100 for every 32-bit m
    while (n > 0xFFFFFFFFull) {
        unsigned long long q = n / 100;
        unsigned k = (unsigned)(n - q * 100) * 2;
        n = q;
        *--d = pairs[k + 1];
        *--d = pairs[k];
    }
    unsigned m = (unsigned)n;
    while (m >= 100) {
        unsigned q = (unsigned)(((unsigned long long)m * 1374389535ull) >> 37);
        unsigned k = (m - q * 100) * 2;
        m = q;
        *--d = pairs[k + 1];
        *--d = pairs[k];
    }
    if (m >= 10) {
        unsigned k = m * 2;
        *--d = pairs[k + 1];
        *--d = pairs[k];
    } else if (m > 0) {
        *--d = (char)('0' + m);
    }
    return d;
}

int quickFloat(char conversion, bool alternate, int precision, double v, char *text, bool *negative)
{
    union { double d; unsigned long long u; } bits;
    bits.d = v;
    *negative = (bits.u >> 63) != 0;
    int biased = (int)((bits.u >> 52) & 0x7FF);
    unsigned long long m = bits.u & 0xFFFFFFFFFFFFFull;
    if (biased == 0x7FF) return -1;
    int e = -1074;
    if (biased != 0) {
        m |= 1ull << 52;
        e = biased - 1075;
    }
    char c = (char)(conversion | 0x20);
    char digits[24];
    char *end = digits + sizeof digits;
    int n = 0;
    if (c == 'f') {
        if (precision > 19) return -1;
        unsigned long long q = 0;
        if (m != 0) {
            unsigned up;
            if (!scaled(m, precision, e, q, up) || q + up < q) return -1;
            q += up;
        }
        char *d = padded(q, precision + 1, end);
        int count = (int)bytesBetween(d, end), whole = count - precision;
        for (int i = 0; i < whole; i++) text[n++] = d[i];
        if (precision > 0 || alternate) text[n++] = '.';
        for (int i = whole; i < count; i++) text[n++] = d[i];
        return n;
    }
    if (c != 'e' && c != 'g') return -1;
    // e: precision + 1 significant digits; g: precision of them, f style while the exponent x is from -4 to below
    // the precision, trailing zeros dropped unless #
    int figures = c == 'e' ? precision + 1 : precision;
    if (figures > 18 || (biased == 0 && m != 0)) return -1;
    int x = 0;
    unsigned long long q = 0;
    if (m != 0 && !significant(m, e, figures - 1, x, q)) return -1;
    if (c == 'g' && figures > x && x >= -4) {
        int fraction = figures - 1 - x;
        char *d = padded(q, fraction + 1, end);
        int count = (int)bytesBetween(d, end), whole = count - fraction;
        if (!alternate)
            while (fraction > 0 && d[count - 1] == '0') { count--; fraction--; }
        for (int i = 0; i < whole; i++) text[n++] = d[i];
        if (fraction > 0 || alternate) text[n++] = '.';
        for (int i = whole; i < count; i++) text[n++] = d[i];
        return n;
    }
    char *d = padded(q, figures, end);
    int count = figures, fraction = figures - 1;
    if (c == 'g' && !alternate)
        while (fraction > 0 && d[count - 1] == '0') { count--; fraction--; }
    text[n++] = d[0];
    if (fraction > 0 || alternate) text[n++] = '.';
    for (int i = 1; i < count; i++) text[n++] = d[i];
    text[n++] = conversion == 'E' || conversion == 'G' ? 'E' : 'e';
    text[n++] = x < 0 ? '-' : '+';
    unsigned ax = (unsigned)(x < 0 ? -x : x), tensDigit = 0;
    while (ax >= 10) { ax -= 10; tensDigit++; }
    text[n++] = (char)('0' + tensDigit);
    text[n++] = (char)('0' + ax);
    return n;
}

// The class of each character from ' ' to 'z': 0 other, 1 %, 2 ., 3 *, 4 0, 5 1-9, 6 a flag (space # + -),
// 7 a length (F h I j l L N t T w z), 8 a conversion (A C E G S X Z a c d e f g i n o p s u x)
unsigned PrintFormat::classOf(unsigned c)
{
    static const unsigned char classes[] = {
        6, 0, 0, 6, 0, 1, 0, 0, 0, 0, 3, 6, 0, 6, 2, 0,     // space ! " # $ % & ' ( ) * + , - . /
        4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0,     // 0-9 : ; < = > ?
        0, 8, 0, 8, 0, 8, 7, 8, 0, 7, 0, 0, 7, 0, 7, 0,     // @ A-O
        0, 0, 0, 8, 7, 0, 0, 0, 8, 0, 8, 0, 0, 0, 0, 0,     // P-Z [ \ ] ^ _
        0, 8, 0, 8, 8, 8, 8, 8, 7, 8, 7, 0, 7, 0, 8, 8,     // ` a-o
        8, 0, 0, 8, 7, 8, 0, 7, 8, 0, 7                     // p-z
    };
    return c < ' ' || c > 'z' ? 0 : classes[c - ' '];
}

// The next state for a character of each class (rows) in each state (columns: normal, %, flag, width, ., precision,
// length, conversion)
PrintFormat::State PrintFormat::nextState(unsigned c, State previous)
{
    static const unsigned char next[9][8] = {
        { Normal, Normal,    Normal, Normal, Normal,    Normal,    Normal, Normal },   // other
        { Percent, Normal,   Normal, Normal, Normal,    Normal,    Normal, Percent },  // %
        { Normal, Dot,       Dot,    Dot,    Normal,    Normal,    Normal, Normal },   // .
        { Normal, Width,     Width,  Normal, Precision, Normal,    Normal, Normal },   // *
        { Normal, Flag,      Flag,   Width,  Precision, Precision, Normal, Normal },   // 0
        { Normal, Width,     Width,  Width,  Precision, Precision, Normal, Normal },   // 1-9
        { Normal, Flag,      Flag,   Normal, Normal,    Normal,    Normal, Normal },   // flag
        { Normal, Size,      Size,   Size,   Size,      Size,      Size,   Normal },   // length
        { Normal, Type,      Type,   Type,   Type,      Type,      Type,   Normal }    // conversion
    };
    return (State)next[classOf(c)][previous];
}

// A wide format's plain %c and %s are wide and its %C and %S narrow; a narrow format's the other way round
bool PrintFormat::wideSpecifier(char conversion, Length length, bool wideFormat)
{
    if (length == L || length == W) return true;
    if (length == H || length == BigT) return false;
    bool plain = conversion == 'c' || conversion == 's';
    return wideFormat ? plain : !plain;
}

unsigned PrintFormat::integerSize(Length length)
{
    switch (length) {
    case None: case L: case I32: return 4;
    case HH: return 1;
    case H: return 2;
    case LL: case J: case Z: case T: case I: case I64: return 8;
    default: return 0;
    }
}

int floatText(char conversion, bool alternate, int precision, va_list *args, char *buf, size_t capacity, char **heap)
{
    char spec[24], digits[12];
    int n = 0, k = 0;
    spec[n++] = '%';
    if (alternate) spec[n++] = '#';
    spec[n++] = '.';
    do digits[k++] = (char)('0' + precision % 10); while ((precision /= 10) != 0);
    while (k) spec[n++] = digits[--k];
    spec[n++] = conversion;
    spec[n] = 0;
    va_list again;
    va_copy(again, *args);
    *heap = 0;
    rts6x::OutputSink out(buf, capacity);
    int length = rts6x::Formatter(out, args).run(rts6x::FormatText(spec));
    if (length < 0 || (size_t)length < capacity) return length;
    char *big = (char *)malloc((size_t)length + 1);
    if (!big) return -1;
    rts6x::OutputSink all(big, (size_t)length + 1);
    rts6x::Formatter(all, &again).run(rts6x::FormatText(spec));
    *heap = big;
    return length;
}

void freeText(char *heap)
{
    if (heap) free(heap);
}

void StreamOut::drain()
{
    if (at_ != 0 && grt::putCall(f_, stage_, (unsigned)at_) < 0) failed_ = true;
    at_ = 0;
}

int printToStream(FILE *f, const char *format, va_list *args)
{
    StreamOut out(f);
    int n = Printer<StreamOut>(out, args).run(format);
    if (n < 0) f->flags |= rts6x::Stream::Failed;
    return n;
}

int printToMemory(char *dst, size_t capacity, bool snprintf, const char *format, va_list *args)
{
    MemoryOut out(dst, capacity);
    int n = Printer<MemoryOut>(out, args).run(format);
    if (n < 0 && snprintf && capacity != 0) dst[0] = 0;
    return n;
}

void WideStreamOut::drain()
{
    if (at_ != 0 && grt::putCall(f_, stage_, (unsigned)at_) < 0) failed_ = true;
    at_ = 0;
}

int wprintToStream(FILE *f, const wchar_t *format, va_list *args)
{
    int info = __grt_descriptor(f->fd);
    WideStreamOut out(f, info >= 0 && (info & GRT_TEXT) != 0);
    int n = Printer<WideStreamOut, wchar_t>(out, args).run(format);
    if (n < 0) f->flags |= rts6x::Stream::Failed;
    return n;
}

int wprintToMemory(wchar_t *dst, size_t capacity, const wchar_t *format, va_list *args)
{
    WideMemoryOut out(dst, capacity);
    return Printer<WideMemoryOut, wchar_t>(out, args).run(format);
}

}  // namespace grt
