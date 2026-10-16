// Spec: ISO C 7.12.4.4 atan2 and F.9.1.4, every case: atan2(+-0, x) is +-0 for x > 0 or +0 and
// +-pi for x < 0 or -0; a zero x gives +-pi/2; the infinities give +-pi/4, +-3pi/4, +-pi/2, +-0
// or +-pi; a subnormal argument; and finite ones whose exponents are more than 60 apart.

#include "ArcTangent.h"
#include "MathBits.h"
#include "MathConstants.h"
#include "MathError.h"

namespace rts6x {

double ArcTangent::edge(double y, double x)
{
    unsigned long long uy = MathBits::of(y), ux = MathBits::of(x);
    if (MathBits::isNaN(uy)) return y;
    if (MathBits::isNaN(ux)) return x;
    bool yNegative = MathBits::negative(uy), xNegative = MathBits::negative(ux);
    DoubleDouble angle;
    if (MathBits::isZero(uy)) {
        if (!xNegative) return y;
        angle = pi();
    } else if (MathBits::isZero(ux)) {
        angle = halfPi();
    } else if (MathBits::isInfinite(uy)) {
        if (!MathBits::isInfinite(ux)) angle = halfPi();
        else angle = xNegative ? pi().times(0.75) : pi().times(0.25);
    } else if (MathBits::isInfinite(ux)) {
        if (!xNegative) return MathBits::zero(yNegative);
        angle = pi();
    } else {
        UnpackedFloat a(uy & MathBits::magnitudeMask(), FloatFormat::binary64());
        UnpackedFloat b(ux & MathBits::magnitudeMask(), FloatFormat::binary64());
        int d = a.exponent() - b.exponent();
        if (d < -60) {
            // |y/x| < 2^-59: atan of it rounds to the quotient itself (a subnormal one included).
            double q = MathBits::absolute(y) / MathBits::absolute(x);
            if (!xNegative) {
                // Below the least normal the quotient has underflowed: a range error (7.12.1/6).
                if (MathBits::below(q, -1022)) MathError::range();
                return MathBits::withSign(q, yNegative);
            }
            angle = DoubleDouble(q, 0.0);
        } else if (d > 60) {
            angle = halfPi().plus(-(MathBits::absolute(x) / MathBits::absolute(y)));
        } else {
            // Both scaled by the same power of two, the larger near one, the smaller still normal.
            int top = a.exponent() > b.exponent() ? a.exponent() : b.exponent();
            double ay = MathBits::from((a.significand() & MathBits::fractionMask()) | ((unsigned long long)(a.exponent() - top + 1023) << 52));
            double ax = MathBits::from((b.significand() & MathBits::fractionMask()) | ((unsigned long long)(b.exponent() - top + 1023) << 52));
            if (ay <= ax) angle = kernel(DoubleDouble(ay, 0.0).over(DoubleDouble(ax, 0.0)));
            else angle = halfPi().minus(kernel(DoubleDouble(ax, 0.0).over(DoubleDouble(ay, 0.0))));
        }
        if (xNegative) angle = pi().minus(angle);
    }
    double v = angle.value();
    return yNegative ? -v : v;
}

}  // namespace rts6x
