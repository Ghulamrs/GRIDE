// Spec: ISO C 7.12.4.1, 7.12.4.2 - sqrt(1 - a^2): 1 - a^2 exactly as a double-double (Dekker's
// product), its root w 1/sqrt(w) to an ulp or two and then corrected once by the exact remainder.

#include "ArcTangent.h"
#include "SquareRoot.h"

namespace rts6x {

DoubleDouble ArcTangent::cosineOf(double a)
{
    DoubleDouble p = DoubleDouble::product(a, a);
    DoubleDouble w = DoubleDouble::sum(1.0, -p.hi);
    w = DoubleDouble::quickSum(w.hi, w.lo - p.lo);
    double y = SquareRoot::inverse(w.hi), s = w.hi * y;
    DoubleDouble q = DoubleDouble::product(s, s);
    double correction = ((w.hi - q.hi) - q.lo + w.lo) * (0.5 * y);
    return DoubleDouble::quickSum(s, correction);
}

}  // namespace rts6x
