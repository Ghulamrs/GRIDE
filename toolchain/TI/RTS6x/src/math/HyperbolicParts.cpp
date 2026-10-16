// Spec: ISO C 7.12.5.4-7.12.5.6 - sinh a = a + a^3/3! + ... to a^11/11! and cosh a = 1 + a^2/2! +
// ... to a^10/10! for a < 1/16 (truncation below 2^-76); e^a +- e^-a from ExpKernel otherwise.

#include "Hyperbolic.h"
#include "ExpKernel.h"
#include "MathBits.h"

namespace rts6x {

DoubleDouble Hyperbolic::smallSine(double a)
{
    double a2 = a * a;
    double tail = a * a2 * (0.16666666666666666 + a2 * (0.008333333333333333 + a2 * (0.0001984126984126984
                  + a2 * (2.7557319223985893e-06 + a2 * 2.505210838544172e-08))));
    return DoubleDouble::quickSum(a, tail);
}

DoubleDouble Hyperbolic::smallCosine(double a)
{
    double a2 = a * a;
    double tail = a2 * (0.5 + a2 * (0.041666666666666664 + a2 * (0.001388888888888889
                  + a2 * (2.48015873015873e-05 + a2 * 2.755731922398589e-07))));
    return DoubleDouble::quickSum(1.0, tail);
}

DoubleDouble Hyperbolic::pair(double a, double sign)
{
    DoubleDouble up, down;
    int ku = ExpKernel::evaluate(DoubleDouble(a, 0.0), up);
    int kd = ExpKernel::evaluate(DoubleDouble(-a, 0.0), down);
    return ExpKernel::scaled(up, ku).plus(ExpKernel::scaled(down, kd).times(sign));
}

double Hyperbolic::halfExp(double a, bool negative)
{
    DoubleDouble m;
    int k = ExpKernel::evaluate(DoubleDouble(a, 0.0), m);
    return MathBits::compose(m, k - 1, negative);
}

}  // namespace rts6x
