// Spec: ISO C 7.12.4.6 sin and its neighbours - Cody and Waite: k = nearest(x 2/pi), then x - k p1
// - k p2 - k p3 - k p4 in a double-double, the first three products exact (33-bit parts, k < 2^20);
// the error below 2^-129 absolute, so an r of at least 2^-59 is good to 2^-70.

#include "ArgumentReduction.h"
#include "MathBits.h"
#include "MathConstants.h"

namespace rts6x {

int ArgumentReduction::reduce(double x, DoubleDouble &r)
{
    bool negative = MathBits::negative(MathBits::of(x));
    double ax = MathBits::absolute(x);
    int q = 0;
    if (ax <= 0.7853981633974483) {
        r = DoubleDouble(x, 0.0);
        return 0;
    }
    bool done = false;
    if (ax < 1048576.0) {
        int k = (int)(ax * MathConstants::twoOverPi + 0.5);
        double kd = (double)k;
        DoubleDouble s = DoubleDouble::sum(ax - kd * MathConstants::halfPiPart1, -kd * MathConstants::halfPiPart2);
        s = s.plus(-kd * MathConstants::halfPiPart3);
        s = s.plus(-kd * MathConstants::halfPiPart4);
        if (!MathBits::below(s.hi, -59)) { r = s; q = k & 3; done = true; }
    }
    if (!done) q = exact(ax, r);
    if (negative) { r = r.negated(); q = (4 - q) & 3; }
    return q;
}

}  // namespace rts6x
