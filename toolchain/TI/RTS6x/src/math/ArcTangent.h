// Spec: ISO C 7.12.4.1 acos, 7.12.4.2 asin, 7.12.4.3 atan, 7.12.4.4 atan2, F.9.1.1-F.9.1.4. One
// kernel: atan y = atan c + atan((y - c)/(1 + y c)) with c = j/16 from a table (the addition
// formula for the arctangent), the rest by its Taylor series; every quotient a double-double.
#ifndef RTS6X_ARC_TANGENT_H
#define RTS6X_ARC_TANGENT_H

#include "DoubleDouble.h"

namespace rts6x {

class ArcTangent {
public:
    static double arcTangent(double x);             // atan
    static double arcTangent2(double y, double x);  // atan2: the angle of (x, y), in [-pi, pi]
    static double arcSine(double x);                // asin: |x| > 1 a domain error
    static double arcCosine(double x);              // acos: |x| > 1 a domain error

    // atan y as a double-double for 0 <= y.hi <= 1 + 2^-20, error about 2^-64 relative.
    static DoubleDouble kernel(const DoubleDouble &y);

private:
    // atan2 for every other pair: the zeros, infinities and NaNs, and exponents far apart.
    static double edge(double y, double x);
    // sqrt(1 - a^2) as a double-double, 0 <= a < 1.
    static DoubleDouble cosineOf(double a);
    static DoubleDouble halfPi();
    static DoubleDouble pi();

    static const double atanHi_[17], atanLo_[17];
    // atan(j/32) for j = 0..32, atan2's finer table.
    static const double atan32Hi_[33], atan32Lo_[33];
};

}  // namespace rts6x

#endif
