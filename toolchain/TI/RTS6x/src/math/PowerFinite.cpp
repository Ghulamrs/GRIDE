// Spec: ISO C 7.12.7.4 pow: x^y = e^z, z = y ln x to about 2^-66 relative - ln x a double-double,
// y ln x an exact product - then ExpKernel, and one rounding; ERANGE past either end (7.12.1).

#include "Power.h"
#include "ExpKernel.h"
#include "Logarithm.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double Power::finite(double x, double y, bool negative)
{
    unsigned long long uy = MathBits::of(y);
    bool up = (MathBits::of(x) > 0x3FF0000000000000ull) != MathBits::negative(uy);
    // |y| >= 2^64 and x != 1: |y ln x| >= 2^64 * 2^-54, far past either end.
    if (MathBits::biased(uy) >= 1023 + 64) return up ? MathError::overflow(negative) : MathError::underflow(negative);
    double lo;
    double hi = Logarithm::kernel(x, lo);
    DoubleDouble l = DoubleDouble::quickSum(hi, lo);
    DoubleDouble p = DoubleDouble::product(y, l.hi);
    DoubleDouble z = DoubleDouble::quickSum(p.hi, p.lo + y * l.lo);
    if (z.hi > 746.0) return MathError::overflow(negative);
    if (z.hi < -746.0) return MathError::underflow(negative);
    DoubleDouble m;
    int k = ExpKernel::evaluate(z, m);
    return MathBits::compose(m, k, negative);
}

}  // namespace rts6x
