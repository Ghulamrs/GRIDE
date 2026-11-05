// Spec: ISO C99 7.12.8.4 tgamma and F.9.5.4 (GammaFunction.h), with the errno the Microsoft runtime's UCRT sets
// (tests\math99): a pole or an overflow HUGE_VAL and ERANGE, a negative integer a domain error, an underflow to zero
// +0 whatever the sign and ERANGE. Rounded once at the end.
#include <math.h>
#include "BinaryScale.h"
#include "ExtendedConstants.h"
#include "GammaFunction.h"
#include "MathBits.h"
#include "MathError.h"
using rts6x::BinaryScale;
using rts6x::DoubleDouble;
using rts6x::ExtendedConstants;
using rts6x::GammaFunction;
using rts6x::MathBits;
using rts6x::MathError;
extern "C" double tgamma(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? x : MathError::domainC99();
    if (MathBits::isZero(u)) return MathError::overflow(MathBits::negative(u));
    if (x < 0 && x == floor(x)) return MathError::domainC99();
    if (x > 171.7) return MathError::overflow(false);
    if (fabs(x) < 5.551115123125783e-17) {
        double r = 1.0 / x;
        if (!MathBits::isFinite(MathBits::of(r))) return MathError::overflow(MathBits::negative(u));
        return r - ExtendedConstants::eulerGamma().hi;
    }
    int k;
    if (x >= 0.5) {
        DoubleDouble m = GammaFunction::gamma(DoubleDouble(x, 0.0), k);
        return BinaryScale::scale(m.value(), k);
    }
    if (x < -190.0) return MathError::underflow(false);
    DoubleDouble g = GammaFunction::gamma(DoubleDouble::sum(1.0, -x), k);
    double r = BinaryScale::scale(ExtendedConstants::pi().over(GammaFunction::sinPi(x).times(g)).value(), -k);
    return MathBits::isZero(MathBits::of(r)) ? 0.0 : r;
}
