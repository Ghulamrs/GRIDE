// Spec: Newton's method on f(y) = 1/y - m: the error squares at each step, from 1/17 for the
// line 24/17 - 8m/17 over m in [1, 2) (the best line through the ends) to below 2^-60 in four.

#include "NewtonIteration.h"
#include "MathBits.h"

namespace rts6x {

double NewtonIteration::reciprocal(double d)
{
    unsigned long long u = MathBits::of(d);
    int e = MathBits::biased(u) - 1023;
    double m = MathBits::from((u & MathBits::fractionMask()) | (1023ull << 52));
    double y = 1.411764705882353 - 0.47058823529411764 * m;
    for (int i = 0; i < 4; i++) y = y * (2.0 - m * y);
    y *= MathBits::powerOfTwo(-e);
    return MathBits::negative(u) ? -y : y;
}

}  // namespace rts6x
