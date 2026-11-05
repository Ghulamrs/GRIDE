// Spec: ISO C99 7.12.8.3 and 7.12.8.4 - sin(pi x) for the reflection: its Taylor series in double-double arithmetic.

#include <math.h>
#include "GammaFunction.h"
#include "ExtendedConstants.h"

namespace rts6x {

DoubleDouble GammaFunction::sinPi(double x)
{
    double r = x - 2.0 * rint(0.5 * x);
    bool negative = r < 0;
    if (negative) r = -r;
    if (r > 0.5) r = 1.0 - r;
    DoubleDouble t = ExtendedConstants::pi().times(r), t2 = t.times(t), term = t, sum = t;
    for (int n = 1; n < 30; n++) {
        term = term.times(t2).over(-(2.0 * n) * (2.0 * n + 1.0));
        sum = sum.plus(term);
        if (fabs(term.hi) <= fabs(sum.hi) * 1e-34) break;
    }
    return negative ? sum.negated() : sum;
}

}  // namespace rts6x
