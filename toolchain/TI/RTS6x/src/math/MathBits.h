// Spec: IEEE 754 binary64: sign, 11-bit biased exponent, 52-bit fraction; subnormals, infinities,
// NaNs. Classification and scaling done on the bits, since the C674x reads a subnormal as zero.
#ifndef RTS6X_MATH_BITS_H
#define RTS6X_MATH_BITS_H

#include "../helpers/SoftFloat.h"

namespace rts6x {

class DoubleDouble;

class MathBits {
public:
    static unsigned long long of(double d) { return FloatBits::of(d); }
    static double from(unsigned long long u) { return FloatBits::toDouble(u); }
    // The two 32-bit words of a double, and a double from them: one store and one load each, where
    // a 64-bit integer costs cpp11 a generic shift for every >> 32.
    static unsigned high(double d) { union { double d; unsigned w[2]; } x; x.d = d; return x.w[1]; }
    static unsigned low(double d) { union { double d; unsigned w[2]; } x; x.d = d; return x.w[0]; }
    static double fromWords(unsigned hi, unsigned lo) { union { double d; unsigned w[2]; } x; x.w[1] = hi; x.w[0] = lo; return x.d; }
    // v * 2^k for a normal v whose product is normal: k added to the exponent field.
    static double scaled(double v, int k)
    {
        union { double d; unsigned w[2]; } x;
        x.d = v;
        x.w[1] += (unsigned)k << 20;
        return x.d;
    }

    static unsigned long long signBit() { return 0x8000000000000000ull; }
    static unsigned long long magnitudeMask() { return 0x7FFFFFFFFFFFFFFFull; }
    static unsigned long long fractionMask() { return 0x000FFFFFFFFFFFFFull; }
    static int biased(unsigned long long u) { return (int)((unsigned)(u >> 32) >> 20 & 0x7FF); }
    static bool negative(unsigned long long u) { return (u >> 63) != 0; }
    static bool isNaN(unsigned long long u) { return (u & magnitudeMask()) > 0x7FF0000000000000ull; }
    static bool isInfinite(unsigned long long u) { return (u & magnitudeMask()) == 0x7FF0000000000000ull; }
    static bool isZero(unsigned long long u) { return (u & magnitudeMask()) == 0; }
    // Finite and not zero: normal or subnormal.
    static bool isFinite(unsigned long long u) { return biased(u) != 0x7FF; }

    static double zero(bool negative) { return from(negative ? signBit() : 0); }
    static double infinity(bool negative) { return from((negative ? signBit() : 0) | 0x7FF0000000000000ull); }
    static double notANumber() { return from(0x7FF8000000000000ull); }
    static double absolute(double x) { return fromWords(high(x) & 0x7FFFFFFFu, low(x)); }
    static double withSign(double magnitude, bool negative)
    {
        return from((of(magnitude) & magnitudeMask()) | (negative ? signBit() : 0));
    }
    // 2^k for -1022 <= k <= 1023.
    static double powerOfTwo(int k) { return fromWords((unsigned)(k + 1023) << 20, 0); }
    // |x| below 2^k, x normal or subnormal or zero: compared on the magnitude's bits.
    static bool below(double x, int k) { return (high(x) & 0x7FFFFFFFu) < (unsigned)(k + 1023) << 20; }

    // (m.hi + m.lo) * 2^k rounded once, m.hi > 0 and normal, negated if asked; past the largest
    // double, ERANGE and infinity; subnormal or zero, ERANGE.
    static double compose(const DoubleDouble &m, int k, bool negative);
};

}  // namespace rts6x

#endif
