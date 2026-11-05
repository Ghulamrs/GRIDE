// Spec: ISO C99 7.12.8.2 erfc and F.9.5.2 (ErrorFunction.h), with the errno the Microsoft runtime's UCRT sets
// (tests\math99): an underflow to zero ERANGE. Rounded once at the end.
#include <math.h>
#include "BinaryScale.h"
#include "ErrorFunction.h"
#include "MathBits.h"
#include "MathError.h"
using rts6x::DoubleDouble;
using rts6x::ErrorFunction;
using rts6x::MathBits;
extern "C" double erfc(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? 0.0 : 2.0;
    if (x < -6.0) return 2.0;
    if (x < 2.0) {
        if (fabs(x) < 1.3877787807814457e-17) return 1.0 - x;
        DoubleDouble e = ErrorFunction::series(fabs(x));
        return (x < 0 ? e.plus(1.0) : DoubleDouble(1.0, 0.0).minus(e)).value();
    }
    if (x > 27.3) return rts6x::MathError::underflow(false);
    int k;
    DoubleDouble m = ErrorFunction::fraction(x, k);
    return rts6x::BinaryScale::scale(m.value(), k);
}
