// Spec: ISO C99 7.12.6.11 logb and F.9.3.11, as the Microsoft runtime gives it (tests\math99): 0 is -inf and ERANGE.
#include <math.h>
#include "BinaryScale.h"
#include "MathBits.h"
#include "MathError.h"
using rts6x::MathBits;
extern "C" double logb(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::isInfinite(u)) return MathBits::infinity(false);
    if (MathBits::isZero(u)) return rts6x::MathError::overflow(true);
    return (double)rts6x::BinaryScale::exponent(u);
}
