// Spec: ISO C 7.12.6.12 modf and F.9.3.12: modf(+-inf) is +-0 with +-inf stored, modf(NaN) is NaN
// with NaN stored; both parts carry x's sign, a zero fraction included.

#include "IntegralPart.h"
#include "MathBits.h"

namespace rts6x {

double IntegralPart::split(double x, double *whole)
{
    unsigned long long u = MathBits::of(x);
    int e = MathBits::biased(u) - 1023;
    bool negative = MathBits::negative(u);
    if (MathBits::isNaN(u)) { *whole = x; return x; }
    if (e >= 52) { *whole = x; return MathBits::zero(negative); }
    if (e < 0) { *whole = MathBits::zero(negative); return x; }
    unsigned long long mask = (1ull << (52 - e)) - 1;
    double integral = MathBits::from(u & ~mask);
    *whole = integral;
    if ((u & mask) == 0) return MathBits::zero(negative);
    // Exact: both are normal and the difference is a multiple of x's ulp, at least 2^-52.
    return x - integral;
}

}  // namespace rts6x
