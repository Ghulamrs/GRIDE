// Spec: ISO C 7.12.4.3 atan - atan y = atan(j/16) + atan t, t = (y - j/16)/(1 + y j/16), |t| <=
// 1/32; atan t - t by its Taylor series to t^11 (truncation below 2^-60 relative).

#include "ArcTangent.h"
#include "MathConstants.h"

namespace rts6x {

DoubleDouble ArcTangent::kernel(const DoubleDouble &y)
{
    int j = (int)(y.hi * 16.0 + 0.5);
    DoubleDouble t = y;
    if (j != 0) {
        double c = j * 0.0625;
        // y.hi - c is exact (within 1/32 of each other); the denominator 1 + y c exactly enough.
        DoubleDouble n = DoubleDouble::sum(y.hi - c, y.lo);
        DoubleDouble p = DoubleDouble::product(y.hi, c);
        DoubleDouble d = DoubleDouble::sum(1.0, p.hi);
        d = DoubleDouble::quickSum(d.hi, d.lo + p.lo + y.lo * c);
        t = n.over(d);
    }
    double t2 = t.hi * t.hi;
    double tail = t.hi * t2 * (-0.3333333333333333 + t2 * (0.2 + t2 * (-0.14285714285714285
                  + t2 * (0.1111111111111111 + t2 * -0.09090909090909091))));
    DoubleDouble s = DoubleDouble::sum(atanHi_[j], t.hi);
    return DoubleDouble::quickSum(s.hi, s.lo + atanLo_[j] + (t.lo - t2 * t.lo + tail));
}

DoubleDouble ArcTangent::halfPi() { return DoubleDouble(MathConstants::halfPiHi, MathConstants::halfPiLo); }

DoubleDouble ArcTangent::pi() { return DoubleDouble(MathConstants::piHi, MathConstants::piLo); }

}  // namespace rts6x
