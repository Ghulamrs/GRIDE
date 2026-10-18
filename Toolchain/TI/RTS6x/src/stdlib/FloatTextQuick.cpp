// Spec: ISO C 7.20.1.3/3-5 and F.5 for the commonest subject sequence - white space, a sign, decimal
// digits with or without a point, an exponent - read in one pass into a 64-bit w and a power of ten;
// anything else (hexadecimal, INF, NAN, more than 19 significant digits) is left to read().

#include "FloatText.h"
#include "DecimalBinary.h"

namespace rts6x {

bool FloatText::quick(const char *s, const char **end, unsigned long long &bits)
{
    const char *p = s;
    while (*p == ' ' || (unsigned)(*p - 9) < 5) p++;
    bool negative = *p == '-';
    if (*p == '-' || *p == '+') p++;
    // Leading zeros first, so that every digit the loops below meet is significant.
    const char *digits = p;
    while (*p == '0') p++;
    unsigned long long w = 0;
    unsigned d;
    const char *from = p;
    while ((d = (unsigned)(*p - '0')) <= 9) { w = w * 10 + d; p++; }
    int n = (int)(p - from), exponent = 0;
    if (*p == '.') {
        p++;
        if (n == 0)
            while (*p == '0') { p++; exponent--; }
        from = p;
        while ((d = (unsigned)(*p - '0')) <= 9) { w = w * 10 + d; p++; }
        n += (int)(p - from);
        exponent -= (int)(p - from);
        if (p - digits == 1) return false;      // a point with no digit on either side
    }
    if (p == digits || n > 19 || *p == 'x' || *p == 'X') return false;
    *end = *p == 'e' || *p == 'E' ? exponentPart(p, exponent) : p;
    if (n == 0) { bits = negative ? 0x8000000000000000ull : 0; return true; }
    if (shortWay64(negative, w, exponent, bits)) return true;
    return DecimalBinary::toBinary64(w, exponent, negative, bits);
}

}  // namespace rts6x
