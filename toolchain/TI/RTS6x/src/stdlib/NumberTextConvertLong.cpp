// Spec: ISO C99 7.20.1.4/2-8 and 7.4.1.10 (white space in the "C" locale): strtoll's and strtoull's subject
// sequence - white space, a sign, a 0x or 0 prefix as the base allows, then digits - read into 64 bits. Past the
// range: ERANGE and LLONG_MAX, LLONG_MIN or ULLONG_MAX; a minus before strtoull's digits negates the value.
// No subject sequence: 0, and the end is the start.
#include "NumberText.h"
#include "../misc/ErrorNumber.h"
namespace rts6x {
unsigned long long NumberText::convertLong(const unsigned char *p, char **end, int base, unsigned isSigned)
{
    const unsigned char *s = p;
    unsigned c = *p;
    while (c == ' ' || (c >= '\t' && c <= '\r')) c = *++p;
    bool negative = false;
    if (c == '+' || c == '-') {
        negative = c == '-';
        c = *++p;
    }
    if (base < 0 || base == 1 || base > 36) {
        if (end) *end = (char *)s;
        return 0;
    }
    unsigned b = (unsigned)base;
#if defined(_WIN32)
    // GRIDE's runtime for Windows: the Microsoft runtime takes a 0x with no hexadecimal digit after it as no
    // number at all, where C would read the 0
    if ((b == 0 || b == 16) && c == '0' && (p[1] | 0x20) == 'x' && digit_[p[2]] >= 16) {
        if (end) *end = (char *)s;
        return 0;
    }
#endif
    if (b == 0) {
        b = 10;
        if (c == '0') {
            b = 8;
            if ((p[1] | 0x20) == 'x' && digit_[p[2]] < 16) {
                b = 16;
                p += 2;
            }
        }
    } else if (b == 16 && c == '0' && (p[1] | 0x20) == 'x' && digit_[p[2]] < 16) {
        p += 2;
    }
    if (digit_[*p] >= b) {
        if (end) *end = (char *)s;
        return 0;
    }
    unsigned long long limit = !isSigned ? 0xFFFFFFFFFFFFFFFFull : negative ? 0x8000000000000000ull : 0x7FFFFFFFFFFFFFFFull;
    unsigned long long v = 0;
    bool over = false;
    for (unsigned d; (d = digit_[*p]) < b; p++) {
        if (over) continue;
        if (v > (limit - d) / b) over = true;
        else v = v * b + d;
    }
    if (end) *end = (char *)p;
    if (over) {
        ErrorNumber::set(34);   // ERANGE
        return limit;
    }
    return negative ? 0 - v : v;
}
}  // namespace rts6x
