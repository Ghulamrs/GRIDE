// Spec: ISO C 7.12.6.6 ldexp and F.9.3.6: x * 2^n, rounded to nearest once (to a subnormal where it
// lands there); +-0, +-inf and NaN returned; overflow to HUGE_VAL and underflow to zero set ERANGE.

#include "BinaryScale.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double BinaryScale::scale(double x, int n)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isZero(u) || !MathBits::isFinite(u)) return x;
    // Past +-2200 every finite x has gone to infinity or zero; the bound keeps the sum an int.
    if (n > 2200) n = 2200;
    if (n < -2200) n = -2200;
    // A normal x that stays normal: the exponent field moved, nothing rounded.
    int biased = MathBits::biased(u);
    if (biased != 0 && biased + n >= 1 && biased + n <= 2046)
        return MathBits::from((u & ~(0x7FFull << 52)) | ((unsigned long long)(biased + n) << 52));
    UnpackedFloat a(u, FloatFormat::binary64());
    unsigned long long r = FloatPacker::pack(FloatFormat::binary64(), a.negative(), a.exponent() + n, a.significand(), false);
    if (MathBits::isInfinite(r) || MathBits::isZero(r)) MathError::range();
    return MathBits::from(r);
}

}  // namespace rts6x
