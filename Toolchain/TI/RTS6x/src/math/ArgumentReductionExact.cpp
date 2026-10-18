// Spec: ISO C 7.12.4.6 sin for huge arguments - M. Payne and R. Hanek, "Radian reduction for
// trigonometric functions" (1983): of x * 2/pi only the bits that are not a multiple of 8 matter,
// so x's 53-bit significand times a 192-bit window of 2/pi gives k mod 8 and the fraction.

#include "ArgumentReduction.h"
#include "MathBits.h"
#include "MathConstants.h"

namespace rts6x {

unsigned ArgumentReduction::window(int pos)
{
    int q = pos >> 5, shift = pos & 31;
    // Word q holds bits 32q .. 32q + 31; twoOverPi_[37] is word 0.
    unsigned low = q >= 0 && q <= 37 ? twoOverPi_[37 - q] : 0;
    unsigned high = q + 1 >= 0 && q + 1 <= 37 ? twoOverPi_[36 - q] : 0;
    return shift == 0 ? low : (low >> shift) | (high << (32 - shift));
}

int ArgumentReduction::exact(double ax, DoubleDouble &r)
{
    UnpackedFloat a(MathBits::of(ax), FloatFormat::binary64());
    // x = S 2^E; bit b of B = floor(2^1216 2/pi) weighs S 2^(E - 1216 + b), a multiple of 8 from
    // b = 1219 - E up: the window is the 192 bits below that.
    int low = 1219 - a.exponent() - 192;
    unsigned w[6], z[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 6; i++) w[i] = window(low + 32 * i);
    unsigned s[2] = { (unsigned)a.significand(), (unsigned)(a.significand() >> 32) };
    for (int i = 0; i < 2; i++) {
        unsigned long long carry = 0;
        for (int j = 0; j < 6 && i + j < 8; j++) {
            unsigned long long t = (unsigned long long)s[i] * w[j] + z[i + j] + carry;
            z[i + j] = (unsigned)t;
            carry = t >> 32;
        }
        if (i + 6 < 8) z[i + 6] = (unsigned)carry;
    }
    // z[5..0] = S w mod 2^192: the top 3 bits are k mod 8, the 189 below the fraction of a turn.
    int q = (int)(z[5] >> 29);
    bool negative = (z[5] >> 28) & 1;
    unsigned f[6];
    for (int i = 0; i < 6; i++) f[i] = z[i];
    f[5] &= 0x1FFFFFFFu;
    if (negative) {
        // The fraction is at least one half: k + 1, and the fraction 1 - f, negative.
        q++;
        unsigned long long borrow = 0;
        for (int i = 0; i < 6; i++) {
            unsigned long long t = (i == 5 ? 0x20000000ull : 0ull) - f[i] - borrow;
            f[i] = (unsigned)t;
            borrow = (t >> 32) & 1;
        }
    }
    int top = 5;
    while (top > 0 && f[top] == 0) top--;
    if (f[top] == 0) { r = DoubleDouble(0.0, 0.0); return q & 3; }
    // The 64 bits under the leading one, then the next 64: two doubles of 53 bits each.
    int lead = 31;
    while (((f[top] >> lead) & 1) == 0) lead--;
    int msb = 32 * top + lead;
    unsigned long long hiBits = 0, loBits = 0;
    for (int b = 0; b < 106; b++) {
        int pos = msb - b;
        unsigned bit = pos >= 0 ? (f[pos >> 5] >> (pos & 31)) & 1 : 0;
        if (b < 53) hiBits = (hiBits << 1) | bit;
        else loBits = (loBits << 1) | bit;
    }
    // The fraction is f 2^-189 turns of pi/2; its leading bit weighs 2^(msb - 189).
    double h = (double)hiBits * MathBits::powerOfTwo(msb - 189 - 52);
    double l = (double)loBits * MathBits::powerOfTwo(msb - 189 - 105);
    DoubleDouble t = DoubleDouble::quickSum(h, l);
    r = t.times(DoubleDouble(MathConstants::halfPiHi, MathConstants::halfPiLo));
    if (negative) r = r.negated();
    return q & 3;
}

}  // namespace rts6x
