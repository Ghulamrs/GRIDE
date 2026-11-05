// Spec: ISO C99 7.12.6.9 log1p, 7.12.5 and 7.12.8: ln a for a double-double a > 0, ln a.hi from the kernel and
// ln(1 + a.lo / a.hi) by its series' first terms.

#include "Logarithm.h"

namespace rts6x {

DoubleDouble Logarithm::extended(const DoubleDouble &a)
{
    double lo;
    double hi = kernel(a.hi, lo);
    double t = a.lo / a.hi;
    return DoubleDouble::quickSum(hi, lo + (t - 0.5 * t * t));
}

}  // namespace rts6x
