// Spec: ISO C 7.20.1.3/3 and /8 - a hexadecimal significand, a binary exponent after p; the value
// rounded to the format, to nearest, ties to even (FLT_ROUNDS 1).

#include "FloatText.h"
#include "../ctype/CharacterClass.h"

namespace rts6x {

unsigned long long FloatText::hexadecimal(const FloatFormat &format, bool negative, const char *s,
                                          const char **end, bool &range)
{
    const char *p = s;
    unsigned long long significand = 0;
    long exponent = 0;
    bool sticky = false, point = false;
    for (;; p++) {
        if (*p == '.' && !point) { point = true; continue; }
        int v = hexValue(*p);
        if (v < 0) break;
        if (significand >> 60) {
            sticky = sticky || v != 0;
            if (!point) exponent += 4;
        } else {
            significand = significand << 4 | (unsigned)v;
            if (point) exponent -= 4;
        }
    }
    if (*p == 'p' || *p == 'P') {
        const char *q = p + 1;
        bool down = false;
        if (*q == '+' || *q == '-') down = *q++ == '-';
        if (CharacterClass::digit((unsigned char)*q)) {
            long e = 0;
            for (; CharacterClass::digit((unsigned char)*q); q++)
                if (e < 100000) e = e * 10 + (*q - '0');
            exponent += down ? -e : e;
            p = q;
        }
    }
    *end = p;
    if (significand == 0) return format.zero(negative);
    if (exponent > 5000) exponent = 5000;
    if (exponent < -5000) exponent = -5000;
    unsigned long long bits = FloatPacker::pack(format, negative, (int)exponent, significand, sticky);
    // Overflow, or a result below the normal range that lost bits on the way.
    unsigned long long magnitude = bits & ~format.signBit();
    int lowest = 1 - format.bias() - format.fractionBits();
    bool lost = sticky || (exponent < lowest && (lowest - exponent >= 64 || (significand & ((1ull << (lowest - exponent)) - 1)) != 0));
    if (magnitude == format.infinity(false) || (magnitude < (1ull << format.fractionBits()) && lost)) range = true;
    return bits;
}

}  // namespace rts6x
