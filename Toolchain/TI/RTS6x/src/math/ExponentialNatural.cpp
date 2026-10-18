// Spec: ISO C 7.12.6.1 exp, F.9.3.1; 7.12.1 for the range errors past ln(DBL_MAX) and below the
// least subnormal.

#include "Exponential.h"
#include "ExpKernel.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double Exponential::natural(double x)
{
    // 2^-54 <= |x| < 704, where e^x is normal: one test of the high word, then no other.
    union { double d; unsigned w[2]; } v;
    v.d = x;
    unsigned top = v.w[1] & 0x7FFFFFFFu;
    if (top - 0x3C900000u < 0x03F60000u) {
        double h, l;
        int k = ExpKernel::evaluate(x, 0.0, h, l);
        v.d = h + l;
        v.w[1] += (unsigned)k << 20;
        return v.d;
    }
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::isInfinite(u)) return MathBits::negative(u) ? 0.0 : x;
    // Below 2^-54, e^x is 1 + x and rounds to 1.
    if (MathBits::below(x, -54)) return 1.0;
    if (x > 710.0) return MathError::overflow(false);
    if (x < -746.0) return MathError::underflow(false);
    double h, l;
    int k = ExpKernel::evaluate(x, 0.0, h, l);
    return MathBits::compose(DoubleDouble::quickSum(h, l), k, false);
}

}  // namespace rts6x
