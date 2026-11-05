// Spec: ISO C99 7.12.5.2 asinh and F.9.2.2: ln(a + sqrt(a^2 + 1)) in double-double arithmetic, rounded once.
#include <math.h>
#include "ExtendedConstants.h"
#include "Logarithm.h"
#include "MathBits.h"
#include "SquareRoot.h"
using rts6x::DoubleDouble;
using rts6x::ExtendedConstants;
using rts6x::Logarithm;
using rts6x::MathBits;
extern "C" double asinh(double x)
{
    if (!MathBits::isFinite(MathBits::of(x))) return x + x;
    double a = fabs(x);
    if (a < 3.725290298461914e-09) return x;
    DoubleDouble r;
    if (a > 268435456.0) r = Logarithm::extended(DoubleDouble(a, 0.0)).plus(ExtendedConstants::lnTwo());
    else r = Logarithm::extended(rts6x::SquareRoot::extended(DoubleDouble::product(a, a).plus(1.0)).plus(a));
    return x < 0 ? -r.value() : r.value();
}
