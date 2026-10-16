// Spec: ISO C 7.12.4.3 atan and F.9.1.3: atan(+-0) is +-0, atan(+-inf) +-pi/2; above 1, atan x =
// pi/2 - atan(1/x) with 1/x a double-double. Below 2^-27 atan x rounds to x; above 2^60 to pi/2.

#include "ArcTangent.h"
#include "MathBits.h"
#include "MathConstants.h"

namespace rts6x {

double ArcTangent::arcTangent(double x)
{
    unsigned long long u = MathBits::of(x);
    bool negative = MathBits::negative(u);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::below(x, -27)) return x;
    double a = MathBits::absolute(x);
    double v;
    if (!MathBits::below(a, 60)) v = MathConstants::halfPiHi;
    else if (a <= 1.0) v = kernel(DoubleDouble(a, 0.0)).value();
    else v = halfPi().minus(kernel(DoubleDouble(1.0, 0.0).over(DoubleDouble(a, 0.0)))).value();
    return negative ? -v : v;
}

}  // namespace rts6x
