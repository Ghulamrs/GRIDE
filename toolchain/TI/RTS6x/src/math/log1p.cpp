// Spec: ISO C99 7.12.6.9 log1p and F.9.3.9, with the errno the Microsoft runtime's UCRT sets (tests\math99): 1 + x
// exactly as a double-double, its logarithm rounded once.
#include <math.h>
#include "Logarithm.h"
#include "MathBits.h"
#include "MathError.h"
using rts6x::DoubleDouble;
using rts6x::MathError;
extern "C" double log1p(double x)
{
    if (x != x) return x + x;
    if (x == -1.0) return MathError::overflow(true);
    if (x < -1.0) return MathError::domainC99();
    if (rts6x::MathBits::isInfinite(rts6x::MathBits::of(x))) return x;
    if (fabs(x) < 5.551115123125783e-17) return x;
    return rts6x::Logarithm::extended(DoubleDouble::sum(1.0, x)).value();
}
