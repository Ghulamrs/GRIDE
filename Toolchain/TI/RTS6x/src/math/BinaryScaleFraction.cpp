// Spec: ISO C 7.12.6.4 frexp and F.9.3.4: frexp(+-0) is +-0, frexp(+-inf) +-inf, frexp(NaN) NaN,
// the exponent stored 0 for each; otherwise a fraction in [0.5, 1) with x's sign.

#include "BinaryScale.h"
#include "MathBits.h"

namespace rts6x {

double BinaryScale::fraction(double x, int *exponent)
{
    unsigned long long u = MathBits::of(x);
    *exponent = 0;
    if (MathBits::isZero(u) || !MathBits::isFinite(u)) return x;
    UnpackedFloat a(u, FloatFormat::binary64());
    *exponent = a.exponent() + 53;
    return MathBits::from((u & MathBits::signBit()) | (1022ull << 52) | (a.significand() & MathBits::fractionMask()));
}

}  // namespace rts6x
