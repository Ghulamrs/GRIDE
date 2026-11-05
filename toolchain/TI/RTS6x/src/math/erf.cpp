// Spec: ISO C99 7.12.8.1 erf and F.9.5.1 (ErrorFunction.h), rounded once at the end.
#include <math.h>
#include "ErrorFunction.h"
#include "ExpKernel.h"
#include "ExtendedConstants.h"
#include "MathBits.h"
using rts6x::DoubleDouble;
using rts6x::ErrorFunction;
using rts6x::MathBits;
extern "C" double erf(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? 1.0 : -1.0;
    double a = fabs(x);
    if (a < 3.725290298461914e-09) return x * rts6x::ExtendedConstants::twoOverRootPi().hi;
    if (a >= 6.0) return x > 0 ? 1.0 : -1.0;
    double r;
    if (a < 2.0) {
        r = ErrorFunction::series(a).value();
    } else {
        int k;
        DoubleDouble c = ErrorFunction::fraction(a, k);
        r = DoubleDouble(1.0, 0.0).minus(rts6x::ExpKernel::scaled(c, k)).value();
    }
    return x < 0 ? -r : r;
}
