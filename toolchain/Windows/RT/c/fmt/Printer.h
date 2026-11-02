// Printer.h - the printf family's format engine on Windows (docs\Runtime_Design.md 10.3), as the Microsoft runtime
// reads a format, so that a program prints the same bytes with either runtime.
//
// Spec: ISO C 7.19.6.1, and the Microsoft runtime's choices where C leaves one or calls the behaviour undefined:
// - The format is read by a table of 9 classes of character and 8 states (classOf, nextState). A character that
//   does not continue a specification ends it and is printed as text: "%y" prints y, "%5.3.2f" prints .2f, and a
//   format ending inside a specification prints nothing more.
// - A width or precision of digits past 2^31-1 ends the call with -1. A second length modifier ("%hld"), the N
//   modifier, L, w or T on an integer, and %n (whose count output the program has not enabled) fail fast, as the
//   runtime's check of its parameters does.
// - Every conversion's text is padded the same way: spaces before, or zeros after the prefix with the 0 flag
//   (so "%05s" gives 000ab), or spaces after with -. A precision on an integer drops the 0 flag.
// - In a narrow format %C, %S and %Z are wide, as are %lc, %ls, %wc, %ws; h and T keep them narrow. A wide
//   character above 0xFF prints nothing for %C (errno EILSEQ) and ends %S with -1, in the "C" locale.
// - %p is 16 upper-case hex digits; a null string prints "(null)".
// - Floating conversions take their digits from RTS6x's (PrintFloat.cpp) and the same padding.
// Out has put(char), put(const char *, size_t), repeat(char, size_t) and finish(), which answers the count of
// characters or -1; rts6x::OutputSink (memory) and grt::StreamOut (a FILE) are the two.
#ifndef GRT_PRINTER_H
#define GRT_PRINTER_H
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include "grt.h"

#ifndef EILSEQ
#define EILSEQ 42
#endif

namespace grt {

// A floating conversion's text without sign padding: precision already defaulted, alternate the # flag. The text
// is in buf when it fits in capacity, else in memory from malloc the caller frees (*heap). Its length, or -1.
int floatText(char conversion, bool alternate, int precision, va_list *args, char *buf, size_t capacity,
              char **heap);
void freeText(char *heap);

class PrintFormat {
protected:
    enum { Left = 1, Sign = 2, Space = 4, Alternate = 8, Zero = 0x10, Signed = 0x20, Negative = 0x40 };
    enum Length { None, HH, H, L, LL, J, Z, T, I, I32, I64, LongDouble, W, BigT };
    enum State { Normal, Percent, Flag, Width, Dot, Precision, Size, Type };

    static unsigned classOf(char c);
    static State nextState(char c, State previous);
    // A decimal number from p, as strtol reads it; false past 2^31-1 (errno as it was). p is left after it.
    static bool number(const char *&p, int &value);
    static bool wideSpecifier(char conversion, Length length);
    // The bytes of an integer of the length given, from the arguments; 0 for a length no integer takes.
    static unsigned integerSize(Length length);
};

template <class Out>
class Printer : PrintFormat {
public:
    Printer(Out &out, va_list *args) : out_(out), args_(args), failed_(false) {}

    int run(const char *format)
    {
        if (!format) __grt_fail_fast();
        const char *p = format;
        for (;;) {
            const char *q = p;
            while (*q && *q != '%') q++;
            if (q != p) out_.put(p, (size_t)(q - p));
            if (!*q) break;
            p = q + 1;
            if (!specification(p) || failed_) break;
        }
        int n = out_.finish();
        return failed_ ? -1 : n;
    }

private:
    // One specification from just after its '%': false when the format has ended or the call has failed.
    bool specification(const char *&p)
    {
        flags_ = 0;
        width_ = 0;
        precision_ = -1;
        length_ = None;
        State state = Percent;
        for (;;) {
            char c = *p++;
            if (!c) { p--; return false; }
            state = nextState(c, state);
            switch (state) {
            case Normal:
                out_.put(c);
                return true;
            case Percent:
                return true;
            case Flag:
                flags_ |= c == '-' ? Left : c == '+' ? Sign : c == ' ' ? Space : c == '#' ? Alternate : Zero;
                break;
            case Width:
                if (c == '*') {
                    width_ = va_arg(*args_, int);
                    if (width_ < 0) { flags_ |= Left; width_ = -width_; }
                } else {
                    p--;
                    if (!number(p, width_)) { failed_ = true; return false; }
                }
                break;
            case Dot:
                precision_ = 0;
                break;
            case Precision:
                if (c == '*') {
                    precision_ = va_arg(*args_, int);
                    if (precision_ < 0) precision_ = -1;
                } else {
                    p--;
                    if (!number(p, precision_)) { failed_ = true; return false; }
                }
                break;
            case Size:
                if (c == 'F') { convert('F'); return true; }
                if (c == 'N' || length_ != None) __grt_fail_fast();
                size(c, p);
                break;
            case Type:
                convert(c);
                return true;
            }
        }
    }

    void size(char c, const char *&p)
    {
        switch (c) {
        case 'h': if (*p == 'h') { p++; length_ = HH; } else length_ = H; break;
        case 'l': if (*p == 'l') { p++; length_ = LL; } else length_ = L; break;
        case 'I':
            if (p[0] == '3' && p[1] == '2') { p += 2; length_ = I32; }
            else if (p[0] == '6' && p[1] == '4') { p += 2; length_ = I64; }
            else if (*p == 'd' || *p == 'i' || *p == 'o' || *p == 'u' || *p == 'x' || *p == 'X') length_ = I;
            break;
        case 'L': length_ = LongDouble; break;
        case 'j': length_ = J; break;
        case 't': length_ = T; break;
        case 'z': length_ = Z; break;
        case 'w': length_ = W; break;
        case 'T': length_ = BigT; break;
        }
    }

    void convert(char c)
    {
        switch (c) {
        case 'c': case 'C': character(c); break;
        case 's': case 'S': string(c); break;
        case 'Z': counted(c); break;
        case 'd': case 'i': flags_ |= Signed; integer(c, 10, false); break;
        case 'u': integer(c, 10, false); break;
        case 'o': integer(c, 8, false); break;
        case 'x': integer(c, 16, false); break;
        case 'X': integer(c, 16, true); break;
        case 'p': precision_ = 16; length_ = I64; integer(c, 16, true); break;
        case 'n': va_arg(*args_, void *); __grt_fail_fast(); break;
        default: floating(c); break;
        }
    }

    // The field: the sign or 0x prefix, padding, the text (narrow, or wide when wide is set), padding.
    void field(char c, const char *text, int length, const unsigned short *wide = 0, int zeros = 0)
    {
        char prefix[3];
        int n = 0;
        if (flags_ & Signed) {
            if (flags_ & Negative) prefix[n++] = '-';
            else if (flags_ & Sign) prefix[n++] = '+';
            else if (flags_ & Space) prefix[n++] = ' ';
        }
        if (((c == 'x' || c == 'X') && (flags_ & Alternate)) || c == 'a' || c == 'A') {
            prefix[n++] = '0';
            prefix[n++] = c == 'X' || c == 'A' ? 'X' : 'x';
        }
        int padding = width_ - (length + zeros) - n;
        if (padding < 0) padding = 0;
        if (!(flags_ & (Left | Zero))) out_.repeat(' ', (size_t)padding);
        out_.put(prefix, (size_t)n);
        if ((flags_ & Zero) && !(flags_ & Left)) out_.repeat('0', (size_t)padding);
        out_.repeat('0', (size_t)zeros);
        if (wide) {
            for (int i = 0; i < length; i++) {
                if (wide[i] > 0xFF) { errno = EILSEQ; failed_ = true; return; }
                out_.put((char)wide[i]);
            }
        } else {
            out_.put(text, (size_t)length);
        }
        if (flags_ & Left) out_.repeat(' ', (size_t)padding);
    }

    void character(char c)
    {
        if (wideSpecifier(c, length_)) {
            unsigned short w = (unsigned short)va_arg(*args_, int);
            if (w > 0xFF) { errno = EILSEQ; return; }    // nothing printed, not even the padding; the call goes on
            char b = (char)w;
            field(c, &b, 1);
        } else {
            char b = (char)(unsigned short)va_arg(*args_, int);
            field(c, &b, 1);
        }
    }

    void string(char c)
    {
        int most = precision_ < 0 ? 0x7FFFFFFF : precision_;
        int n = 0;
        if (wideSpecifier(c, length_)) {
            const unsigned short *w = va_arg(*args_, const unsigned short *);
            static const unsigned short none[] = { '(', 'n', 'u', 'l', 'l', ')', 0 };
            if (!w) w = none;
            while (n < most && w[n]) n++;
            field(c, 0, n, w);
        } else {
            const char *s = va_arg(*args_, const char *);
            if (!s) s = "(null)";
            while (n < most && s[n]) n++;
            field(c, s, n);
        }
    }

    // %Z: an ANSI_STRING or UNICODE_STRING (length in bytes, maximum, buffer)
    void counted(char c)
    {
        struct Counted { unsigned short length, maximum; char *buffer; };
        const Counted *s = va_arg(*args_, const Counted *);
        if (!s || !s->buffer) field('s', "(null)", 6);
        else if (wideSpecifier(c, length_)) field(c, 0, s->length / 2, (const unsigned short *)s->buffer);
        else field(c, s->buffer, s->length);
    }

    void integer(char c, unsigned radix, bool upper)
    {
        unsigned size = integerSize(length_);
        if (size == 0) __grt_fail_fast();
        bool forceOctal = radix == 8 && (flags_ & Alternate);
        long long value;
        bool sign = (flags_ & Signed) != 0;
        if (size == 8) value = va_arg(*args_, long long);
        else {
            int v = va_arg(*args_, int);
            if (size == 1) value = sign ? (long long)(signed char)v : (long long)(unsigned char)v;
            else if (size == 2) value = sign ? (long long)(short)v : (long long)(unsigned short)v;
            else value = sign ? (long long)v : (long long)(unsigned)v;
        }
        unsigned long long n = (unsigned long long)value;
        if (sign && value < 0) { n = 0ull - n; flags_ |= Negative; }
        if (size != 8) n &= 0xFFFFFFFFull;
        int precision = precision_;
        if (precision < 0) precision = 1;
        else flags_ &= ~(unsigned)Zero;
        if (n == 0) flags_ &= ~(unsigned)Alternate;
        char digits[24];
        char *end = digits + sizeof digits, *d = end;
        if (radix == 10) {
            static const char pairs[] =
                "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
                "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
                "8081828384858687888990919293949596979899";
            while (n >= 100) {
                unsigned k = (unsigned)(n % 100) * 2;
                n /= 100;
                *--d = pairs[k + 1];
                *--d = pairs[k];
            }
            if (n >= 10) {
                unsigned k = (unsigned)n * 2;
                *--d = pairs[k + 1];
                *--d = pairs[k];
            } else if (n > 0) {
                *--d = (char)('0' + n);
            }
        } else {
            const char *hexits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
            unsigned shift = radix == 16 ? 4 : 3, mask = radix - 1;
            while (n) { *--d = hexits[n & mask]; n >>= shift; }
        }
        int length = (int)(end - d);
        int zeros = precision > length ? precision - length : 0;
        if (forceOctal && zeros == 0) zeros = 1;
        field(c, d, length, 0, zeros);
    }

    void floating(char c)
    {
        flags_ |= Signed;
        int precision = precision_;
        if (precision < 0) precision = c == 'a' || c == 'A' ? 13 : 6;
        else if (precision == 0 && (c == 'g' || c == 'G')) precision = 1;
        char buf[400];
        char *heap = 0;
        int n = floatText(c, (flags_ & Alternate) != 0, precision, args_, buf, sizeof buf, &heap);
        if (n < 0) { failed_ = true; return; }
        const char *text = heap ? heap : buf;
        if (*text == '-') { flags_ |= Negative; text++; n--; }
        if ((c == 'a' || c == 'A') && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { text += 2; n -= 2; }
        if (*text == 'i' || *text == 'I' || *text == 'n' || *text == 'N') {
            flags_ &= ~(unsigned)Zero;
            if (c == 'a' || c == 'A') c = 's';
        }
        field(c, text, n);
        freeText(heap);
    }

    Out &out_;
    va_list *args_;
    bool failed_;
    unsigned flags_;
    int width_, precision_;
    Length length_;
};

// A FILE's characters for one call: staged here, then put into the stream's buffer (translated in text mode) at
// most once per 2,048 bytes, so a console is written once per call of that much, as the Microsoft runtime's is.
// (A stage of a page would need __chkstk, which only the layer above grtc has.)
class StreamOut {
public:
    explicit StreamOut(FILE *f) : f_(f), at_(0), count_(0), failed_(false) {}
    void put(char c) { if (at_ == Stage) drain(); stage_[at_++] = c; count_++; }
    void put(const char *s, size_t n) { while (n) { if (at_ == Stage) drain(); size_t k = Stage - at_; if (k > n) k = n; copy(s, k); s += k; n -= k; } }
    void repeat(char c, size_t n) { while (n) { if (at_ == Stage) drain(); size_t k = Stage - at_; if (k > n) k = n; fill(c, k); n -= k; } }
    int finish() { drain(); return failed_ ? -1 : (int)count_; }

private:
    enum { Stage = 2048 };
    void copy(const char *s, size_t n) { for (size_t i = 0; i < n; i++) stage_[at_ + i] = s[i]; at_ += n; count_ += n; }
    void fill(char c, size_t n) { for (size_t i = 0; i < n; i++) stage_[at_ + i] = c; at_ += n; count_ += n; }
    void drain();

    FILE *f_;
    size_t at_, count_;
    bool failed_;
    char stage_[Stage];
};

// The printf family's two ends: to a stream (the stream locked by the caller), and into memory with sprintf's
// or snprintf's NUL (capacity counts the NUL; a failed call leaves dst as the Microsoft runtime's does).
int printToStream(FILE *f, const char *format, va_list *args);
int printToMemory(char *dst, size_t capacity, bool snprintf, const char *format, va_list *args);

}  // namespace grt

#endif
