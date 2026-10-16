// Spec: ISO C 7.12.5.6 tanh and F.9.2.6: tanh(+-0) is +-0, tanh(+-inf) +-1; below 2^-27 tanh x
// rounds to x, from 22 up to +-1 (1 - tanh 22 < 2^-62). Between, sinh/cosh below 1/16 and
// (e^2a - 1)/(e^2a + 1) above it, each a double-double quotient rounded once.

#include "Hyperbolic.h"
#include "ExpKernel.h"
#include "MathBits.h"

namespace rts6x {

double Hyperbolic::tangent(double x)
{
    unsigned long long u = MathBits::of(x);
    bool negative = MathBits::negative(u);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::below(x, -27)) return x;
    double a = MathBits::absolute(x), v;
    if (a >= 22.0) v = 1.0;
    else if (a < 0.0625) v = smallSine(a).over(smallCosine(a)).value();
    else {
        DoubleDouble m;
        int k = ExpKernel::evaluate(DoubleDouble(2.0 * a, 0.0), m);
        DoubleDouble e = ExpKernel::scaled(m, k);
        v = e.plus(-1.0).over(e.plus(1.0)).value();
    }
    return negative ? -v : v;
}

}  // namespace rts6x
