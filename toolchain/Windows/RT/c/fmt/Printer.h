// Printer.h - the printf family's format engine on Windows (docs\Runtime_Design.md 10.3), as the Microsoft runtime
// reads a format, so that a program prints the same bytes with either runtime.
//
// Spec: ISO C 7.19.6.1 and 7.24.2.1, and the Microsoft runtime's choices where C leaves one or calls the behaviour
// undefined:
// - The format is read by a table of 9 classes of character and 8 states (classOf, nextState). A character that
//   does not continue a specification ends it and is printed as text: "%y" prints y, "%5.3.2f" prints .2f, and a
//   format ending inside a specification prints nothing more.
// - A width or precision of digits past 2^31-1 ends the call with -1. A second length modifier ("%hld"), the N
//   modifier, L, w or T on an integer, and %n (whose count output the program has not enabled) fail fast, as the
//   runtime's check of its parameters does.
// - Every conversion's text is padded the same way: spaces before, or zeros after the prefix with the 0 flag
//   (so "%05s" gives 000ab), or spaces after with -. A precision on an integer drops the 0 flag.
// - In a narrow format %C, %S and %Z are wide, as are %lc, %ls, %wc, %ws; h and T keep them narrow. A wide
//   character above 0xFF prints nothing for %C (errno EILSEQ) and ends %S with -1, in the "C" locale. Once setlocale
//   asks for UTF-8 they print its bytes, a surrogate pair of %S one character, and only a lone surrogate fails so.
// - A wide format (the wprintf family) is read the same way, with the runtime's legacy meanings: there %c and %s
//   are wide and %C and %S narrow, a narrow string's bytes each the wide character of its value, whatever the
//   encoding. To a text stream a wide character goes out as narrowUnit makes it.
// - %p is 16 upper-case hex digits; a null string prints "(null)".
// - Floating conversions take their digits from RTS6x's (PrintFloat.cpp) and the same padding.
// Out has put for a character and a run of the format's characters, put(const char *, size_t) for narrow text,
// putWide for wide text (false where a wide character has no byte), repeat(char, size_t), finish(), which answers
// the count of characters or -1, and Wide; MemoryOut, StreamOut, WideMemoryOut and WideStreamOut are the four.
#ifndef GRT_PRINTER_H
#define GRT_PRINTER_H
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
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
// f F e E g G of a finite value whose digits a 64-bit integer holds, the text floatText would give without its sign
// (at most 32 characters), and *negative the sign bit; -1 for a value or precision left to floatText
int quickFloat(char conversion, bool alternate, int precision, double v, char *text, bool *negative);
// n's decimal digits written backward, ending before end; their start (end itself for 0)
char *decimalDigits(unsigned long long n, char *end);

// A code unit in the program's narrow encoding: its bytes into to, at most three, and how many; -1 where it has none,
// past 0xFF in the "C" locale, and a surrogate in UTF-8, which is half a character.
int narrowUnit(unsigned c, char *to);
// The character at s[*at] of a wide string of n units in the same encoding, at most four bytes: how many, *at left on
// its last unit. -1 where it has none; in UTF-8 a surrogate pair is one character, and a first surrogate that ends the
// string is 0 bytes, as the Microsoft runtime's.
int narrowCharacter(const unsigned short *s, int n, int *at, char *to);

// A wide string as narrow text, for Out's putWide: false at a unit that has no bytes
template <class Out>
bool putNarrow(Out &out, const unsigned short *s, int n)
{
    for (int i = 0; i < n; i++) {
        char b[4];
        int k = narrowCharacter(s, n, &i, b);
        if (k < 0) return false;
        out.put(b, (size_t)k);
    }
    return true;
}

class PrintFormat {
protected:
    enum { Left = 1, Sign = 2, Space = 4, Alternate = 8, Zero = 0x10, Signed = 0x20, Negative = 0x40 };
    enum Length { None, HH, H, L, LL, J, Z, T, I, I32, I64, LongDouble, W, BigT };
    enum State { Normal, Percent, Flag, Width, Dot, Precision, Size, Type };

    static unsigned code(char c) { return (unsigned char)c; }
    static unsigned code(wchar_t c) { return c; }
    static unsigned classOf(unsigned c);
    static State nextState(unsigned c, State previous);
    // A decimal number from p, as strtol reads it; false past 2^31-1 (errno as it was). p is left after it.
    template <class Ch>
    static bool number(const Ch *&p, int &value)
    {
        long long v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p++ - '0');
            if (v > 0x7FFFFFFFLL) {
                while (*p >= '0' && *p <= '9') p++;
                return false;
            }
        }
        value = (int)v;
        return true;
    }
    static bool wideSpecifier(char conversion, Length length, bool wideFormat);
    // The bytes of an integer of the length given, from the arguments; 0 for a length no integer takes.
    static unsigned integerSize(Length length);
};

template <class Out, class Ch = char>
class Printer : PrintFormat {
public:
    Printer(Out &out, va_list *args) : out_(out), args_(args), failed_(false) {}

    int run(const Ch *format)
    {
        if (!format) __grt_fail_fast();
        const Ch *p = format;
        for (;;) {
            const Ch *q = p;
            while (*q && *q != '%') q++;
            if (q != p) out_.put(p, sizeof(Ch) == 1 ? bytesBetween(p, q) : bytesBetween(p, q) >> 1);
            if (!*q) break;
            p = q + 1;
            if (!specification(p) || failed_) break;
        }
        int n = out_.finish();
        return failed_ ? -1 : n;
    }

private:
    // One specification from just after its '%': false when the format has ended or the call has failed.
    bool specification(const Ch *&p)
    {
        flags_ = 0;
        width_ = 0;
        precision_ = -1;
        length_ = None;
        State state = Percent;
        for (;;) {
            Ch c = *p++;
            if (!c) { p--; return false; }
            state = nextState(code(c), state);
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
                size((char)c, p);
                break;
            case Type:
                convert((char)c);
                return true;
            }
        }
    }

    void size(char c, const Ch *&p)
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

    bool wide(char conversion) { return wideSpecifier(conversion, length_, sizeof(Ch) != 1); }

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
        if (padding && !(flags_ & (Left | Zero))) out_.repeat(' ', (size_t)padding);
        if (n) out_.put(prefix, (size_t)n);
        if (padding && (flags_ & Zero) && !(flags_ & Left)) out_.repeat('0', (size_t)padding);
        if (zeros) out_.repeat('0', (size_t)zeros);
        if (wide) {
            if (!out_.putWide(wide, length)) { errno = EILSEQ; failed_ = true; return; }
        } else {
            out_.put(text, (size_t)length);
        }
        if (padding && (flags_ & Left)) out_.repeat(' ', (size_t)padding);
    }

    void character(char c)
    {
        if (wide(c)) {
            unsigned short w = (unsigned short)va_arg(*args_, int);
            char bytes[3];
            int n = Out::Wide ? 1 : narrowUnit(w, bytes);
            if (n < 0) { errno = EILSEQ; return; }                    // nothing printed, not even the padding; the call goes on
            if (n > 1) field(c, bytes, n);
            else field(c, 0, 1, &w);
        } else {
            char b = (char)(unsigned short)va_arg(*args_, int);
            field(c, &b, 1);
        }
    }

    void string(char c)
    {
        int most = precision_ < 0 ? 0x7FFFFFFF : precision_;
        int n = 0;
        if (wide(c)) {
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
        else if (wide(c)) field(c, 0, s->length / 2, (const unsigned short *)s->buffer);
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
            d = decimalDigits(n, end);
        } else {
            const char *hexits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
            unsigned shift = radix == 16 ? 4 : 3, mask = radix - 1;
            while (n) { *--d = hexits[n & mask]; n >>= shift; }
        }
        int length = (int)bytesBetween(d, end);
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
        va_list peek;
        va_copy(peek, *args_);
        double v = va_arg(peek, double);
        va_end(peek);
        char quick[32];
        bool negative;
        int k = quickFloat(c, (flags_ & Alternate) != 0, precision, v, quick, &negative);
        if (k >= 0) {
            (void)va_arg(*args_, double);
            if (negative) flags_ |= Negative;
            field(c, quick, k);
            return;
        }
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
    enum { Wide = 0 };
    explicit StreamOut(FILE *f) : f_(f), at_(0), count_(0), failed_(false) {}
    void put(char c) { if (at_ == Stage) drain(); stage_[at_++] = c; count_++; }
    void put(const char *s, size_t n) { while (n) { if (at_ == Stage) drain(); size_t k = Stage - at_; if (k > n) k = n; copy(s, k); s += k; n -= k; } }
    bool putWide(const unsigned short *s, int n) { return putNarrow(*this, s, n); }
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

// Memory for sprintf and snprintf, as RTS6x's OutputSink fills it: capacity bytes at dst with the NUL's among
// them, the characters past them counted but not written. The room left is a count, not a difference of pointers.
class MemoryOut {
public:
    enum { Wide = 0 };
    MemoryOut(char *dst, size_t capacity) : next_(dst), room_(0), count_(0), capacity_(capacity)
    {
        if (capacity != 0) {
            size_t most = (size_t)-1 - (size_t)dst;
            room_ = capacity - 1 < most ? capacity - 1 : most;
        }
    }
    void put(char c) { if (room_) { *next_++ = c; room_--; } count_++; }
    void put(const char *s, size_t n)
    {
        size_t k = n < room_ ? n : room_;
        if (k > 16) memcpy(next_, s, k);
        else for (size_t i = 0; i < k; i++) next_[i] = s[i];
        next_ += k;
        room_ -= k;
        count_ += n;
    }
    bool putWide(const unsigned short *s, int n) { return putNarrow(*this, s, n); }
    void repeat(char c, size_t n)
    {
        size_t k = n < room_ ? n : room_;
        for (size_t i = 0; i < k; i++) next_[i] = c;
        next_ += k;
        room_ -= k;
        count_ += n;
    }
    int finish() { if (capacity_ != 0) *next_ = 0; return (int)count_; }

private:
    char *next_;
    size_t room_, count_, capacity_;
};

// A FILE's characters for a wide format, as the Microsoft runtime's wide printf writes a stream: in text mode a
// code unit is its narrow bytes (narrowUnit), where it has none '?' in the "C" locale and nothing in UTF-8; in binary
// mode its two bytes, low first. Staged as StreamOut is.
class WideStreamOut {
public:
    enum { Wide = 1 };
    WideStreamOut(FILE *f, bool text) : f_(f), text_(text), at_(0), count_(0), failed_(false) {}
    void put(wchar_t c) { unit(c); count_++; }
    void put(char c) { unit((unsigned char)c); count_++; }
    void put(const char *s, size_t n) { for (size_t i = 0; i < n; i++) put(s[i]); }
    void put(const wchar_t *s, size_t n) { for (size_t i = 0; i < n; i++) put(s[i]); }
    bool putWide(const unsigned short *s, int n) { for (int i = 0; i < n; i++) put((wchar_t)s[i]); return true; }
    void repeat(char c, size_t n) { while (n--) put(c); }
    int finish() { drain(); return failed_ ? -1 : (int)count_; }

private:
    enum { Stage = 2048 };
    void unit(unsigned c)
    {
        if (at_ + 3 > Stage) drain();
        if (text_) {
            int k = narrowUnit(c, stage_ + at_);
            if (k > 0) at_ += (size_t)k;
            else if (!__grt_utf8) stage_[at_++] = '?';
        }
        else { stage_[at_++] = (char)(c & 0xFF); stage_[at_++] = (char)(c >> 8); }
    }
    void drain();

    FILE *f_;
    bool text_;
    size_t at_, count_;
    bool failed_;
    char stage_[Stage];
};

// Memory for swprintf and vswprintf: capacity code units at dst with the NUL's among them. A text that does not fit
// is written as far as it goes with its NUL and answered -1, as the Microsoft runtime's swprintf does.
class WideMemoryOut {
public:
    enum { Wide = 1 };
    WideMemoryOut(wchar_t *dst, size_t capacity) : dst_(dst), capacity_(capacity), count_(0) {}
    void put(wchar_t c) { if (count_ + 1 < capacity_) dst_[count_] = c; count_++; }
    void put(char c) { put((wchar_t)(unsigned char)c); }
    void put(const char *s, size_t n) { for (size_t i = 0; i < n; i++) put(s[i]); }
    void put(const wchar_t *s, size_t n) { for (size_t i = 0; i < n; i++) put(s[i]); }
    bool putWide(const unsigned short *s, int n) { for (int i = 0; i < n; i++) put((wchar_t)s[i]); return true; }
    void repeat(char c, size_t n) { while (n--) put(c); }
    int finish()
    {
        if (capacity_ == 0) return -1;
        dst_[count_ < capacity_ ? count_ : capacity_ - 1] = 0;
        return count_ < capacity_ ? (int)count_ : -1;
    }

private:
    wchar_t *dst_;
    size_t capacity_, count_;
};

// The printf family's ends: to a stream (the stream locked by the caller), and into memory with sprintf's or
// snprintf's NUL (capacity counts the NUL; a failed call leaves dst as the Microsoft runtime's does); the wprintf
// family's, to a stream and into swprintf's memory.
int printToStream(FILE *f, const char *format, va_list *args);
int printToMemory(char *dst, size_t capacity, bool snprintf, const char *format, va_list *args);
int wprintToStream(FILE *f, const wchar_t *format, va_list *args);
int wprintToMemory(wchar_t *dst, size_t capacity, const wchar_t *format, va_list *args);

}  // namespace grt

#endif
