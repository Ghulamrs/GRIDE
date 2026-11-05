// Spec: ISO C99 7.12.5.1 acosh and F.9.2.1, with the errno the Microsoft runtime's UCRT sets (tests\math99): below 1 a
// domain error. ln(x + sqrt(x^2 - 1)) in double-double arithmetic, rounded once.
#include <math.h>
#include "ExtendedConstants.h"
#include "Logarithm.h"
#include "MathBits.h"
#include "MathError.h"
#include "SquareRoot.h"
using rts6x::DoubleDouble;
using rts6x::Logarithm;
extern "C" double acosh(double x)
{
    if (x != x) return x + x;
    if (x < 1.0) return rts6x::MathError::domainC99();
    if (rts6x::MathBits::isInfinite(rts6x::MathBits::of(x))) return x;
    if (x == 1.0) return 0.0;
    if (x > 268435456.0) return Logarithm::extended(DoubleDouble(x, 0.0)).plus(rts6x::ExtendedConstants::lnTwo()).value();
    return Logarithm::extended(rts6x::SquareRoot::extended(DoubleDouble::product(x, x).plus(-1.0)).plus(x)).value();
}
