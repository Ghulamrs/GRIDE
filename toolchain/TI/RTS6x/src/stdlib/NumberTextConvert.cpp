// Spec: ISO C 7.20.1.4/2-8 and 7.4.1.10 (white space in the "C" locale): strtol's and strtoul's
// subject sequence read once without a test per digit; a run longer than always fits in 32 bits is
// judged afterwards - in base 10 by its length and leading digit, in the rest by a tested reading.

#include <errno.h>
#include <limits.h>
#include "NumberText.h"
#include "../misc/ErrorNumber.h"

namespace rts6x {

unsigned long NumberText::convert(const unsigned char *p, char **end, int base, unsigned isSigned)
{
    const unsigned char *s = p;
    unsigned c = *p;
    while (c <= ' ' && ((c == ' ') | (c - '\t' <= '\r' - '\t'))) c = *++p;
    unsigned negative = c - '+';
    if ((negative | 2) == 2) {
        // '+' or '-', which are two apart.
        negative >>= 1;
        c = *++p;
    } else {
        negative = 0;
    }
    unsigned b = (unsigned)base;
    if (b != 10) {
        if (b - 2 > 34) {
            // 0 chooses by the prefix; 1, past 36 and negative bases have no subject sequence.
            if (b != 0) goto none;
            b = 10;
            if (c == '0') {
                b = 8;
                if ((p[1] | 0x20) == 'x' && digit_[p[2]] < 16) {
                    b = 16;
                    p += 2;
                    c = *p;
                }
            }
        } else if (b == 16 && c == '0' && (p[1] | 0x20) == 'x' && digit_[p[2]] < 16) {
            p += 2;
            c = *p;
        }
    }
    {
        const unsigned char *first = p;
        unsigned long v = 0;
        unsigned d;
        unsigned over = 0;
        if (b == 10) {
            d = c - '0';
            if (d > 9) goto none;
            do {
                c = p[1];
                p++;
                v = (v << 3) + (v << 1) + d;
                d = c - '0';
            } while (d <= 9);
            if (p - first > 9) {
                // Ten significant digits led by 0-3 are below 2^32, led by 5-9 above it; led by 4,
                // T is in [4e9, 5e9): v = T mod 2^32 is T itself only if it is at least 4e9.
                const unsigned char *q = first;
                while (*q == '0') q++;
                long n = p - q;
                over = (n > 10) | ((n == 10) & ((*q > '4') | ((*q == '4') & (v < 4000000000u))));
            }
        } else {
            const unsigned char *digit = digit_;
            d = digit[c];
            if (d >= b) goto none;
            do {
                c = p[1];
                p++;
                v = v * b + d;
                d = digit[c];
            } while (d < b);
            if (p - first > safe_[b]) {
                // More digits than always fit: read again, each tested before it is taken in.
                unsigned long most = limit_[b];
                v = 0;
                for (const unsigned char *q = first; q < p; q++) {
                    d = digit[*q];
                    over |= v > most;
                    v = v * b + d;
                    over |= v < d;
                }
            }
        }
        if (end) *end = (char *)p;
        // strtol's range is one wider below zero; strtoul negates any magnitude it can hold.
        over |= isSigned & (v > (unsigned long)LONG_MAX + negative);
        if (over) {
            ErrorNumber::set(ERANGE);
            if (!isSigned) return ULONG_MAX;
            return negative ? (unsigned long)LONG_MIN : (unsigned long)LONG_MAX;
        }
        return (v ^ (0ul - negative)) + negative;
    }
none:
    if (end) *end = (char *)s;
    return 0;
}

}  // namespace rts6x
