// Spec: W. D. Clinger, "How to Read Floating Point Numbers Accurately", PLDI 1990, section 4: when
// the digits' integer w and 10^k are both exact in the format, w x 10^k or w / 10^k rounded once by
// IEEE 754 arithmetic (correctly rounded, 5.4.1) is the correctly rounded value of the decimal.

#include "FloatText.h"

namespace rts6x {

namespace {

// 10^0 .. 10^22, every one exact in binary64 (5^22 < 2^53); the first eleven exact in binary32.
const double tens[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14,
                        1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
const float tensSingle[] = { 1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f };

}  // namespace

bool FloatText::shortWay64(bool negative, unsigned long long w, int exponent, unsigned long long &bits)
{
    if (w > 0x1FFFFFFFFFFFFFull || exponent < -22) return false;
    // Past 10^22, as many tens as keep w exact move into w first ("1e30" is 10^8 x 10^22).
    for (; exponent > 22; exponent--) {
        if (w > 0x3333333333333ull) return false;
        w *= 10;
    }
    union { unsigned long long u; unsigned word[2]; double d; } x;
    x.u = w;
    double d = (double)x.word[1] * 4294967296.0 + (double)x.word[0];
    x.d = exponent < 0 ? d / tens[-exponent] : d * tens[exponent];
    bits = negative ? x.u | 0x8000000000000000ull : x.u;
    return true;
}

bool FloatText::shortWay(const FloatFormat &format, bool negative, unsigned long long w, int exponent,
                         unsigned long long &bits)
{
    if (format.precision() == 53) return shortWay64(negative, w, exponent, bits);
    if (format.precision() != 24 || w > 0xFFFFFF || exponent < -10 || exponent > 10) return false;
    float f = (float)(unsigned)w;
    f = exponent < 0 ? f / tensSingle[-exponent] : f * tensSingle[exponent];
    bits = FloatBits::of(negative ? -f : f);
    return true;
}

}  // namespace rts6x
