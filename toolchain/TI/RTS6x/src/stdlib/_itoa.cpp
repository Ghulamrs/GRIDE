// Spec: Microsoft C runtime, _itoa - value in radix 2 to 36 into buf with a NUL, lowercase digits; a '-' in
// radix 10 alone, any other radix writing a negative value's 32 bits unsigned (`_itoa(-1, b, 16)` is "ffffffff").
// buf is answered. A radix outside 2..36 leaves buf empty and sets EINVAL, where the UCRT ends the program.

#include <errno.h>
#include <stdlib.h>
#include "../misc/ErrorNumber.h"

extern "C" char *_itoa(int value, char *buf, int radix)
{
    if (radix < 2 || radix > 36) {
        buf[0] = 0;
        errno = EINVAL;
        return buf;
    }
    const bool negative = radix == 10 && value < 0;
    unsigned u = negative ? 0u - static_cast<unsigned>(value) : static_cast<unsigned>(value);
    const unsigned r = static_cast<unsigned>(radix);
    char digits[33];
    int n = 0;
    do {
        unsigned d = u % r;
        digits[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + (d - 10));
        u /= r;
    } while (u != 0);
    char *p = buf;
    if (negative) *p++ = '-';
    while (n != 0) *p++ = digits[--n];
    *p = 0;
    return buf;
}