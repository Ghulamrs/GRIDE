// Spec: ISO C99 7.12.8.1 erf - the series of positive terms of ErrorFunction.h.

#include "ErrorFunction.h"
#include "ExpKernel.h"
#include "ExtendedConstants.h"

namespace rts6x {

DoubleDouble ErrorFunction::series(double a)
{
    DoubleDouble a2 = DoubleDouble::product(a, a);
    DoubleDouble twoA2(2.0 * a2.hi, 2.0 * a2.lo);
    DoubleDouble term(1.0, 0.0), sum(1.0, 0.0);
    for (int n = 1; n < 400; n++) {
        term = term.times(twoA2).over(2.0 * n + 1.0);
        sum = sum.plus(term);
        if (term.hi < sum.hi * 1e-34) break;
    }
    int k;
    DoubleDouble e = ExpKernel::extended(a2.negated(), k);
    e = ExpKernel::scaled(e, k);
    return ExtendedConstants::twoOverRootPi().times(e).times(sum).times(a);
}

}  // namespace rts6x
