// Spec: ISO C 7.12.6.7 log, 7.12.6.8 log10 and F.9.3.7, F.9.3.8. x = 2^e * m, m = (1 + u) / r_j
// with r_j near 1/(1 + j/128) to 8 bits from a table, so u = m r_j - 1 is exact (P. Tang's table
// method); ln x = e ln2 - ln r_j + ln(1 + u), the last by its Taylor series to u^10 (|u| < 2^-7.4).
#ifndef RTS6X_LOGARITHM_H
#define RTS6X_LOGARITHM_H

#include "DoubleDouble.h"

namespace rts6x {

class Logarithm {
public:
    static double natural(double x);    // log
    static double common(double x);     // log10
    // ln x = the result + lo, x finite and above zero (subnormal allowed): about 2^-70 relative.
    static double kernel(double x, double &lo);

private:
    // True with result set for a zero (pole: ERANGE, -inf), x < 0 (EDOM), +inf or a NaN.
    static bool special(double x, double &result);

    static const double reciprocal_[128], logHi_[128], logLo_[128];
};

}  // namespace rts6x

#endif
