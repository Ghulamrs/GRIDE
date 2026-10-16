// Spec: ISO C 7.19.6.1/7-8: d i signed and o u x X unsigned, at the width the length modifier
// names; the precision a minimum digit count (0 with 0 gives none); # adds 0 for o, 0x for x;
// a 0 flag is ignored when a precision is given. p is a pointer, written here as 0x and hex.
#include "Formatter.h"
#include "IntegerDigits.h"

namespace rts6x {

void Formatter::integer(const FormatSpec &spec)
{
    char c = spec.conversion();
    FormatSpec::Length length = spec.length();
    bool wide = length == FormatSpec::LongLong || length == FormatSpec::IntMax ||
                (length == FormatSpec::Long && sizeof(long) > sizeof(int));
    unsigned hi = 0, lo;
    bool negative = false;
    if (c == 'd' || c == 'i') {
        if (wide) {
            long long v = va_arg(*args_, long long);
            negative = v < 0;
            unsigned long long m = negative ? 0ull - (unsigned long long)v : (unsigned long long)v;
            hi = (unsigned)(m >> 32);
            lo = (unsigned)m;
        } else {
            int v = va_arg(*args_, int);
            if (length == FormatSpec::Char) v = (signed char)v;
            else if (length == FormatSpec::Short) v = (short)v;
            negative = v < 0;
            lo = negative ? 0u - (unsigned)v : (unsigned)v;
        }
    } else if (wide) {
        unsigned long long m = va_arg(*args_, unsigned long long);
        hi = (unsigned)(m >> 32);
        lo = (unsigned)m;
    } else {
        lo = va_arg(*args_, unsigned);
        if (length == FormatSpec::Char) lo = (unsigned char)lo;
        else if (length == FormatSpec::Short) lo = (unsigned short)lo;
    }
    unsigned base = c == 'o' ? 8 : (c == 'x' || c == 'X') ? 16 : 10;
    unsignedField(spec, hi, lo, negative, base);
}

void Formatter::unsignedField(const FormatSpec &spec, unsigned hi, unsigned lo, bool negative, unsigned base)
{
    IntegerDigits digits(hi, lo, base, spec.upper());
    size_t n = digits.length();
    size_t zeros = 0;
    if (spec.precision() > 0 && (size_t)spec.precision() > n) zeros = (size_t)spec.precision() - n;
    if (n == 0 && spec.precision() < 0) zeros = 1;                      // zero is "0"
    if (base == 8 && spec.alternate() && zeros == 0) zeros = 1;          // and #o always leads with 0

    char prefix[2];
    size_t p = 0;
    char c = spec.conversion();
    if (c == 'd' || c == 'i') p = signPrefix(spec, negative, prefix);
    else if (base == 16 && spec.alternate() && n != 0) { prefix[0] = '0'; prefix[1] = c; p = 2; }

    // open() and close() inline: zero padding is more leading zeros, space padding goes left or right
    size_t used = p + zeros + n, width = spec.width() > 0 ? (size_t)spec.width() : 0;
    size_t padding = width > used ? width - used : 0;
    bool left = spec.leftAlign();
    if (!left && spec.zeroPad() && spec.precision() < 0) { zeros += padding; padding = 0; }
    if (!left) out_.repeat(' ', padding);
    out_.put(prefix, p);
    out_.repeat('0', zeros);
    out_.put(digits.text(), n);
    if (left) out_.repeat(' ', padding);
}

void Formatter::pointer(const FormatSpec &spec)
{
    void *v = va_arg(*args_, void *);
    IntegerDigits digits(0, (unsigned)(size_t)v, 16, false);
    size_t n = digits.length();
    size_t padding = open(spec, "0x", 2, n == 0 ? 1 : n, false);
    if (n == 0) out_.put('0');
    else out_.put(digits.text(), n);
    close(spec, padding);
}

}  // namespace rts6x
