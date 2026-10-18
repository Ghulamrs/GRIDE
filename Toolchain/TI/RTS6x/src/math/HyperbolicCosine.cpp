// Spec: ISO C 7.12.5.4 cosh and F.9.2.4: cosh(+-0) is 1, cosh(+-inf) +inf; below 2^-27 cosh x
// rounds to 1; above 38, e^|x|/2 alone; above 711, overflow (ERANGE).

#include "Hyperbolic.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double Hyperbolic::cosine(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::isInfinite(u)) return MathBits::infinity(false);
    if (MathBits::below(x, -27)) return 1.0;
    double a = MathBits::absolute(x);
    if (a <= 38.0) return pair(a, 1.0).times(0.5).value();
    if (a <= 711.0) return halfExp(a, false);
    return MathError::overflow(false);
}

}  // namespace rts6x
