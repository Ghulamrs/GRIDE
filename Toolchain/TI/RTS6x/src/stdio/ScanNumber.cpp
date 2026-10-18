// Spec: ISO C 7.19.6.2/12 - d (strtol, base 10), i (base 0), o (8), u (10), x X (16), p (as %x);
// a e f g as strtod. The longest prefix within the width is taken, one character pushed back at
// most, and a field that is not a whole number is a matching failure.

#include "Scanner.h"
#include "../ctype/CharacterClass.h"
#include "../stdlib/FloatText.h"

namespace rts6x {

namespace {

int digitIn(int c, int base)
{
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
    return v < base ? v : -1;
}

}  // namespace

bool Scanner::integer(char conversion, bool assign, int width, Length length, bool &input)
{
    int base = conversion == 'd' || conversion == 'u' ? 10 : conversion == 'o' ? 8 : conversion == 'i' ? 0 : 16;
    int left = width > 0 ? width : 0x7FFFFFFF;
    int c = in_.get();
    bool negative = false;
    if (c == '+' || c == '-') {
        negative = c == '-';
        c = advance(left);
    }
    int digits = 0;
    if (c == '0' && (base == 0 || base == 16)) {
        digits = 1;
        c = advance(left);
        if (c == 'x' || c == 'X') {
            base = 16;
            digits = 0;
            c = advance(left);
        } else if (base == 0) {
            base = 8;
        }
    }
    if (base == 0) base = 10;
    unsigned long long value = 0;
    for (; c >= 0 && digitIn(c, base) >= 0; digits++, c = advance(left))
        value = value * (unsigned)base + (unsigned)digitIn(c, base);
    giveBack(c);
    if (digits == 0) {
        input = c == EOF;
        return false;
    }
    if (negative) value = 0ull - value;
    if (!assign) return true;
    if (conversion == 'p') {
        *va_arg(*args_, void **) = (void *)(unsigned)value;
    } else {
        switch (length) {
        case Char: *va_arg(*args_, char *) = (char)value; break;
        case Short: *va_arg(*args_, short *) = (short)value; break;
        case Long: *va_arg(*args_, long *) = (long)value; break;
        case LongLong: *va_arg(*args_, long long *) = (long long)value; break;
        default: *va_arg(*args_, int *) = (int)value; break;
        }
    }
    assigned_++;
    return true;
}

bool Scanner::floating(bool assign, int width, Length length, bool &input)
{
    char text[MaxText];
    int n = floatText(width > 0 && width < MaxText - 1 ? width : MaxText - 1, text);
    text[n] = 0;
    const char *end = text;
    bool range;
    bool single = length == Default;
    unsigned long long bits = 0;
    if (n > 0) bits = FloatText::read(single ? FloatFormat::binary32() : FloatFormat::binary64(), text, &end, range);
    // A field whose end is not part of a number - "1.5e" - converts as far as it is one.
    if (n == 0 || end == text) {
        input = n == 0 && in_.endReached();
        return false;
    }
    if (!assign) return true;
    if (single) *va_arg(*args_, float *) = FloatBits::toFloat(bits);
    else *va_arg(*args_, double *) = FloatBits::toDouble(bits);
    assigned_++;
    return true;
}

}  // namespace rts6x
