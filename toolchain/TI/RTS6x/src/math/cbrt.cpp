// Spec: ISO C99 7.12.7.1 cbrt and F.9.4.1 - the real cube root; cbrt(+-0) is +-0, cbrt(+-inf) is +-inf, a NaN
// stays a NaN, and no value is an error. A first guess from the exponent divided by three (W. Kahan's) is refined
// by Newton's iteration in the form r + (x / r^2 - r) / 3, which cannot overflow, then corrected once by the
// residual x - r^3 with r^3 an exact double-double: an exact cube gives its exact root. Subnormals and values
// near the top of the range are scaled by powers of two whose exponents are multiples of three. A subnormal
// argument of hypot is left to the target's floating point (the C6747's flushes it to zero, as it does everywhere).
#include <math.h>
#include "MathBits.h"
#include "DoubleDouble.h"
using rts6x::DoubleDouble;
using rts6x::MathBits;
extern "C" double cbrt(double x)
{
    unsigned long long u = MathBits::of(x);
    if (!MathBits::isFinite(u) || MathBits::isZero(u)) return x + x;
    bool negative = MathBits::negative(u);
    double a = MathBits::from(u & MathBits::magnitudeMask()), back = 1.0;
    int e = MathBits::biased(MathBits::of(a));
    if (e == 0) {
        // a subnormal is its fraction times 2^-1074, and 1074 = 3 * 358: the fraction as a double (exact, and
        // made without subnormal arithmetic, which the C6747's floating point flushes to zero)
        a = (double)(long long)(u & MathBits::fractionMask());
        back = MathBits::powerOfTwo(-358);
    } else if (e > 1023 + 960) {
        a *= MathBits::powerOfTwo(-99);
        back = MathBits::powerOfTwo(33);
    }
    double r = MathBits::fromWords(MathBits::high(a) / 3u + 715094163u, 0u);
    for (int i = 0; i < 5; i++) r += (a / (r * r) - r) / 3.0;
    DoubleDouble r2 = DoubleDouble::product(r, r);
    DoubleDouble r3 = r2.times(r);
    r += ((a - r3.hi) - r3.lo) / (3.0 * r2.hi);
    return MathBits::withSign(r * back, negative);
}
