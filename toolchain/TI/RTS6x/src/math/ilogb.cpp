// Spec: ISO C99 7.12.6.5 ilogb, as the Microsoft runtime gives it (tests\math99): 0 is INT_MIN, an infinity or a NaN
// INT_MAX, and no errno.
#include <limits.h>
#include <math.h>
#include "BinaryScale.h"
#include "MathBits.h"
using rts6x::MathBits;
extern "C" int ilogb(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isZero(u)) return INT_MIN;
    if (!MathBits::isFinite(u)) return INT_MAX;
    return rts6x::BinaryScale::exponent(u);
}
