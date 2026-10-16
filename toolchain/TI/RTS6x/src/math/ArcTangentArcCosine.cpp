// Spec: ISO C 7.12.4.1 acos and F.9.1.1: acos(1) is +0, |x| > 1 a domain error. For |x| above
// 1/sqrt(2) acos |x| = atan(sqrt(1 - x^2) / |x|), else pi/2 - atan(|x| / sqrt(1 - x^2)); a
// negative x gives pi - acos |x|. Below 2^-60 acos x rounds to pi/2.

#include "ArcTangent.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double ArcTangent::arcCosine(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::below(x, -60)) return halfPi().value();
    double a = MathBits::absolute(x);
    if (a > 1.0) return MathError::domain();
    DoubleDouble angle;
    if (a == 1.0) angle = DoubleDouble(0.0, 0.0);
    else {
        DoubleDouble c = cosineOf(a);
        if (a > 0.7071067811865476) angle = kernel(c.over(DoubleDouble(a, 0.0)));
        else angle = halfPi().minus(kernel(DoubleDouble(a, 0.0).over(c)));
    }
    if (MathBits::negative(u)) angle = pi().minus(angle);
    return angle.value();
}

}  // namespace rts6x
