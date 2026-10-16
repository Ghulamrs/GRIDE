// Spec: ISO C 7.12.5.4 cosh, 7.12.5.5 sinh, 7.12.5.6 tanh and F.9.2.4-F.9.2.6, from e^x and e^-x
// as double-doubles (ExpKernel); below 1/16, where e^x - e^-x would cancel, sinh and tanh by the
// Taylor series of sinh and cosh instead.
#ifndef RTS6X_HYPERBOLIC_H
#define RTS6X_HYPERBOLIC_H

#include "DoubleDouble.h"

namespace rts6x {

class Hyperbolic {
public:
    static double sine(double x);      // sinh: overflow a range error (ERANGE, +-HUGE_VAL)
    static double cosine(double x);    // cosh: overflow a range error (ERANGE, +HUGE_VAL)
    static double tangent(double x);   // tanh: +-1 at +-inf

private:
    // For 0 <= a < 1/16: sinh a and cosh a as double-doubles, from their Taylor series.
    static DoubleDouble smallSine(double a);
    static DoubleDouble smallCosine(double a);
    // For 0 <= a <= 38: e^a + e^-a (sign 1) or e^a - e^-a (sign -1) as a double-double.
    static DoubleDouble pair(double a, double sign);
    // For a > 38: e^a / 2 rounded once; ERANGE past the largest double.
    static double halfExp(double a, bool negative);
};

}  // namespace rts6x

#endif
