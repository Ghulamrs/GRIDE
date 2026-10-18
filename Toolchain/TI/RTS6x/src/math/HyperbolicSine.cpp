// Spec: ISO C 7.12.5.5 sinh and F.9.2.5: sinh(+-0) is +-0, sinh(+-inf) +-inf; below 2^-27 sinh x
// rounds to x; above 38, e^|x|/2 alone (e^-2|x| < 2^-109); above 711, overflow (ERANGE).

#include "Hyperbolic.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double Hyperbolic::sine(double x)
{
    unsigned long long u = MathBits::of(x);
    bool negative = MathBits::negative(u);
    if (!MathBits::isFinite(u)) return x;
    if (MathBits::below(x, -27)) return x;
    double a = MathBits::absolute(x), v;
    if (a < 0.0625) v = smallSine(a).value();
    else if (a <= 38.0) v = pair(a, -1.0).times(0.5).value();
    else if (a <= 711.0) return halfExp(a, negative);
    else return MathError::overflow(negative);
    return negative ? -v : v;
}

}  // namespace rts6x
