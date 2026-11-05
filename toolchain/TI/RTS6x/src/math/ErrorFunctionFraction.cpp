// Spec: ISO C99 7.12.8.2 erfc - Laplace's continued fraction of ErrorFunction.h, evaluated from its depth up.

#include "ErrorFunction.h"
#include "ExpKernel.h"
#include "ExtendedConstants.h"

namespace rts6x {

DoubleDouble ErrorFunction::fraction(double a, int &k)
{
    int depth = 40 + (int)(300.0 / (a * a));
    DoubleDouble t(a, 0.0);
    for (int n = depth; n >= 1; n--) t = DoubleDouble(0.5 * n, 0.0).over(t).plus(a);
    DoubleDouble e = ExpKernel::extended(DoubleDouble::product(a, a).negated(), k);
    return e.times(ExtendedConstants::oneOverRootPi()).over(t);
}

}  // namespace rts6x
