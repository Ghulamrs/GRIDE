// Spec: ISO C 7.12.10.1 fmod; F.9.7.1: fmod(+-0, y) is +-0 for y not zero, fmod(x, +-inf) is x
// for x finite, an infinite x or a zero y is a domain error, and a NaN operand gives a NaN.

#include "Remainder.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double Remainder::truncated(double x, double y)
{
    unsigned long long ux = MathBits::of(x), uy = MathBits::of(y);
    if (MathBits::isNaN(ux)) return x;
    if (MathBits::isNaN(uy)) return y;
    if (MathBits::isInfinite(ux) || MathBits::isZero(uy)) return MathError::domain();
    if (MathBits::isInfinite(uy) || MathBits::isZero(ux)) return x;
    if ((ux & MathBits::magnitudeMask()) < (uy & MathBits::magnitudeMask())) return x;
    UnpackedFloat a(ux, FloatFormat::binary64()), b(uy, FloatFormat::binary64());
    // x = mx * 2^ex and y = my * 2^ey, ex >= ey since |x| >= |y| and both significands are normalised.
    unsigned long long r = a.significand() % b.significand(), my = b.significand();
    int steps = a.exponent() - b.exponent();
    while (steps > 0 && r != 0) {
        int s = steps < 11 ? steps : 11;
        r = (r << s) % my;
        steps -= s;
    }
    if (r == 0) return MathBits::zero(a.negative());
    return MathBits::from(FloatPacker::pack(FloatFormat::binary64(), a.negative(), b.exponent(), r, false));
}

}  // namespace rts6x
