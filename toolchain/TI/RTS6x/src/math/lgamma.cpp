// Spec: ISO C99 7.12.8.3 lgamma and F.9.5.3 (GammaFunction.h), with the errno the Microsoft runtime's UCRT sets
// (tests\math99): a pole or an overflow HUGE_VAL and ERANGE. Rounded once at the end.
#include <math.h>
#include "ExtendedConstants.h"
#include "GammaFunction.h"
#include "Logarithm.h"
#include "MathBits.h"
#include "MathError.h"
using rts6x::DoubleDouble;
using rts6x::GammaFunction;
using rts6x::Logarithm;
using rts6x::MathBits;
using rts6x::MathError;
extern "C" double lgamma(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return MathBits::infinity(false);
    if (MathBits::isZero(u) || (x < 0 && x == floor(x))) return MathError::overflow(false);
    if (fabs(x) < 5.551115123125783e-17) return -Logarithm::extended(DoubleDouble(fabs(x), 0.0)).value();
    if (x > 0) {
        if (fabs(x - 1.0) < 0.2) return GammaFunction::nearOne(x - 1.0);
        if (fabs(x - 2.0) < 0.2) return GammaFunction::nearTwo(x - 2.0);
        if (x > 1e280) {
            double r = x * (log(x) - 1.0);
            if (!MathBits::isFinite(MathBits::of(r))) return MathError::overflow(false);
            return r;
        }
        return GammaFunction::logGamma(DoubleDouble(x, 0.0)).value();
    }
    DoubleDouble s = GammaFunction::sinPi(x);
    if (s.hi < 0) s = s.negated();
    return rts6x::ExtendedConstants::lnPi().minus(Logarithm::extended(s)).minus(GammaFunction::logGamma(DoubleDouble::sum(1.0, -x))).value();
}
