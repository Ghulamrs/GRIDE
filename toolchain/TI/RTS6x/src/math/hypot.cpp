// Spec: ISO C99 7.12.7.3 hypot and F.9.4.3 - the square root of x^2 + y^2 without undue overflow or underflow;
// hypot(+-inf, y) is +inf even when y is a NaN; a result too large for a double is a range error (ERANGE).
// Both are scaled by a power of two when either is far from 1, the sum of their squares formed exactly as a
// double-double, and its square root corrected once by the exact residual: within half an ulp but for a few
// cases at a rounding boundary.
#include <math.h>
#include "MathBits.h"
#include "MathError.h"
#include "DoubleDouble.h"
using rts6x::DoubleDouble;
using rts6x::MathBits;
extern "C" double hypot(double x, double y)
{
    unsigned long long ux = MathBits::of(x) & MathBits::magnitudeMask();
    unsigned long long uy = MathBits::of(y) & MathBits::magnitudeMask();
    if (MathBits::isInfinite(ux) || MathBits::isInfinite(uy)) return MathBits::infinity(false);
    if (MathBits::isNaN(ux) || MathBits::isNaN(uy)) return x + y;
    double a = MathBits::from(ux), b = MathBits::from(uy);
    if (a < b) {
        double t = a;
        a = b;
        b = t;
    }
    if (b == 0.0) return a;
    int ea = MathBits::biased(MathBits::of(a)), eb = MathBits::biased(MathBits::of(b));
    // b below 2^-60 a: the square root of a^2 + b^2 is a to within far less than half an ulp
    if (ea - eb > 60) return a;
    double scale = 1.0, back = 1.0;
    if (ea > 1023 + 500) {
        scale = MathBits::powerOfTwo(-600);
        back = MathBits::powerOfTwo(600);
    } else if (eb < 1023 - 500) {
        scale = MathBits::powerOfTwo(600);
        back = MathBits::powerOfTwo(-600);
    }
    a *= scale;
    b *= scale;
    DoubleDouble s = DoubleDouble::product(a, a).plus(DoubleDouble::product(b, b));
    double r = sqrt(s.hi);
    DoubleDouble rr = DoubleDouble::product(r, r);
    r += (((s.hi - rr.hi) - rr.lo) + s.lo) / (2.0 * r);
    r *= back;
    if (MathBits::isInfinite(MathBits::of(r))) return rts6x::MathError::overflow(false);
    return r;
}
