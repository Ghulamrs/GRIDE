// Spec: ISO C 7.20.1.3/3-5 - the subject sequence: an optional sign, then a nonempty decimal or
// hexadecimal significand with an optional exponent, or INF, INFINITY, NAN, NAN(n-char-sequence),
// case ignored. An exponent letter is taken only with digits after it.

#include "FloatText.h"
#include "BigNumber.h"
#include "../ctype/CharacterClass.h"

namespace rts6x {

int FloatText::hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool FloatText::matchWord(const char *s, const char *word)
{
    for (; *word; s++, word++)
        if (CharacterClass::toLower((unsigned char)*s) != *word) return false;
    return true;
}

unsigned long long FloatText::read(const FloatFormat &format, const char *s, const char **end, bool &range)
{
    const char *p = s;
    range = false;
    *end = s;
    while (CharacterClass::space((unsigned char)*p)) p++;
    bool negative = false;
    if (*p == '+' || *p == '-') negative = *p++ == '-';
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
        && (hexValue(p[2]) >= 0 || (p[2] == '.' && hexValue(p[3]) >= 0)))
        return hexadecimal(format, negative, p + 2, end, range);
    if (CharacterClass::digit((unsigned char)*p) || (*p == '.' && CharacterClass::digit((unsigned char)p[1])))
        return decimal(format, negative, p, end, range);
    unsigned long long bits = special(format, negative, p, end);
    if (*end == p) *end = s;
    return bits;
}

unsigned long long FloatText::nanPayload(const char *s, const char *stop)
{
    unsigned base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    else if (s[0] == '0') base = 8;
    unsigned long long v = 0;
    for (; s < stop; s++) {
        int d = hexValue(*s);
        if (d < 0 || (unsigned)d >= base) return 0;
        v = v * base + (unsigned)d;
    }
    return v;
}

unsigned long long FloatText::special(const FloatFormat &format, bool negative, const char *s, const char **end)
{
    *end = s;
    if (matchWord(s, "infinity")) { *end = s + 8; return format.infinity(negative); }
    if (matchWord(s, "inf")) { *end = s + 3; return format.infinity(negative); }
    if (!matchWord(s, "nan")) return format.zero(negative);
    const char *p = s + 3;
    *end = p;
#if defined(_WIN32)
    // grt: the Microsoft runtime's NaNs - NAN(IND) the indefinite one (the sign and the quiet bit alone, whatever
    // sign is written), NAN(SNAN) a signalling one (the lowest fraction bit), any other every fraction bit set
    if (*p == '(') {
        const char *q = p + 1;
        while (CharacterClass::alnum((unsigned char)*q) || *q == '_') q++;
        if (*q == ')') {
            *end = q + 1;
            if (q - p == 4 && matchWord(p + 1, "ind")) return format.nan() | format.signBit();
            if (q - p == 5 && matchWord(p + 1, "snan")) return format.infinity(negative) | 1u;
        }
    }
    return format.infinity(negative) | format.fractionMask();
#endif
    unsigned long long payload = 0;
    if (*p == '(') {
        const char *q = p + 1;
        while (CharacterClass::alnum((unsigned char)*q) || *q == '_') q++;
        if (*q == ')') {
            *end = q + 1;
            payload = nanPayload(p + 1, q);
        }
    }
    payload &= format.fractionMask() >> 1;
    return format.nan() | payload | (negative ? format.signBit() : 0);
}

}  // namespace rts6x
