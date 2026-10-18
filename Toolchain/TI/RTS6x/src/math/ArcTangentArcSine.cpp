// Spec: ISO C 7.12.4.2 asin and F.9.1.2: asin(+-0) is +-0, |x| > 1 a domain error. asin x =
// atan(x / sqrt(1 - x^2)), or pi/2 - atan(sqrt(1 - x^2) / x) above 1/sqrt(2), so the kernel's
// argument stays in [0, 1]. Below 2^-27 asin x rounds to x.

#include "ArcTangent.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double ArcTangent::arcSine(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::below(x, -27)) return x;
    double a = MathBits::absolute(x);
    if (a > 1.0) return MathError::domain();
    double v;
    if (a == 1.0) v = halfPi().value();
    else {
        DoubleDouble c = cosineOf(a);
        if (a <= 0.7071067811865476) v = kernel(DoubleDouble(a, 0.0).over(c)).value();
        else v = halfPi().minus(kernel(c.over(DoubleDouble(a, 0.0)))).value();
    }
    return MathBits::negative(u) ? -v : v;
}

}  // namespace rts6x
