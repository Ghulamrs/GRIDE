// Spec: ISO C99 7.12.6.10 log2 and F.9.3.10: ln x times 1/ln 2, both double-doubles, rounded once - so an
// exact power of two gives its exact exponent - as log10 is formed (LogarithmCommon.cpp).
#include "Logarithm.h"
namespace rts6x {
namespace {
// 1/ln 2 = 1.442695040888963407359924681001892137... as a double and the rest
const double invLn2Hi = 1.4426950408889634;
const double invLn2Lo = 2.0355273740931033e-17;
}  // namespace
double Logarithm::binary(double x)
{
    double result;
    if (special(x, result)) return result;
    double lo;
    double hi = kernel(x, lo);
    DoubleDouble l = DoubleDouble::quickSum(hi, lo);
    return l.times(DoubleDouble(invLn2Hi, invLn2Lo)).value();
}
}  // namespace rts6x
