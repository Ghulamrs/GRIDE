// Spec: IEEE 754 binary64; Dekker (1971): a quotient as a double-double. d.hi's reciprocal by
// Newton's method; each quotient digit a product by it, corrected by the exact remainder, so three
// digits leave an error near 2^-100 relative whatever the reciprocal's last bits.

#include "DoubleDouble.h"
#include "NewtonIteration.h"

namespace rts6x {

DoubleDouble DoubleDouble::over(const DoubleDouble &d) const
{
    double reciprocal = NewtonIteration::reciprocal(d.hi);
    double q1 = hi * reciprocal;
    DoubleDouble r = minus(d.times(q1));
    double q2 = r.hi * reciprocal;
    r = r.minus(d.times(q2));
    DoubleDouble q = quickSum(q1, q2);
    return q.plus(r.hi * reciprocal);
}

}  // namespace rts6x
