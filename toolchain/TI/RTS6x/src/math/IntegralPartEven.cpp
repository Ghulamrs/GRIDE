// Spec: ISO C99 7.12.9.3 nearbyint, 7.12.9.4 rint and F.9.6.3, F.9.6.4: to an integer with halfway cases to even, on
// the bits, so nothing here is inexact to the floating point and a subnormal is a number; a zero keeps x's sign, an
// infinity is returned, a NaN made quiet by x + x.

#include "IntegralPart.h"
#include "MathBits.h"

namespace rts6x {

double IntegralPart::nearestEven(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    int e = MathBits::biased(u) - 1023;
    if (e >= 52) return x;
    bool negative = MathBits::negative(u);
    if (e < -1) return MathBits::zero(negative);
    if (e == -1) return (u & MathBits::fractionMask()) ? MathBits::withSign(1.0, negative) : MathBits::zero(negative);
    unsigned long long half = 1ull << (51 - e), units = half << 1, rest = u & (units - 1);
    // units is the bit of the integer's last place. For e = 0 that is the exponent's low bit, 1 in 1023, as the
    // hidden one it stands in for is.
    bool up = rest > half || (rest == half && (u & units));
    u -= rest;
    return MathBits::from(up ? u + units : u);     // a carry into the exponent is IEEE's own rounding
}

}  // namespace rts6x
