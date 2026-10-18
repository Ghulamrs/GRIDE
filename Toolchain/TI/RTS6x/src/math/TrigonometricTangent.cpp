// Spec: ISO C 7.12.4.7 tan and F.9.1.7: tan(+-0) is +-0, tan(+-inf) a domain error; tan r =
// sin r / cos r with both double-doubles and one rounding, -cos r / sin r in the odd quadrants.

#include "Trigonometric.h"
#include "ArgumentReduction.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double Trigonometric::tangent(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::isInfinite(u)) return MathError::domain();
    if (MathBits::below(x, -27)) return x;
    DoubleDouble r;
    int q = ArgumentReduction::reduce(x, r);
    DoubleDouble s = sineKernel(r), c = cosineKernel(r);
    if (q & 1) return -c.over(s).value();
    return s.over(c).value();
}

}  // namespace rts6x
