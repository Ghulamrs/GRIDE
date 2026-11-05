// Spec: ISO C99 7.12.5.1 acosh and 7.12.5.2 asinh: the square root of a double-double, the double root corrected once
// by its exact residual (Dekker's product).

#include <math.h>
#include "SquareRoot.h"

namespace rts6x {

DoubleDouble SquareRoot::extended(const DoubleDouble &v)
{
    double s = sqrt(v.hi);
    DoubleDouble square = DoubleDouble::product(s, s);
    double correction = (((v.hi - square.hi) - square.lo) + v.lo) / (2.0 * s);
    return DoubleDouble::quickSum(s, correction);
}

}  // namespace rts6x
