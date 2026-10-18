// Spec: ISO C 7.12.6.1 exp; W. J. Cody and W. Waite, "Software Manual for the Elementary Functions"
// (1980): e^z = 2^k * 2^(j/128) * e^r with z = (128k + j) ln2/128 + r, |r| <= ln2/256, the product
// ln2/128 * n subtracted in two parts so the first is exact; e^r by its Taylor series to r^6.
#ifndef RTS6X_EXP_KERNEL_H
#define RTS6X_EXP_KERNEL_H

#include "DoubleDouble.h"

namespace rts6x {

class ExpKernel {
public:
    // e^(zh + zl) = (mh + ml) 2^k to 2^-67, k returned, |zh| <= 746, |zl| < 2^-40; ml not normalised.
    static int evaluate(double zh, double zl, double &mh, double &ml);
    // The same with m normalised as a double-double, |m.lo| <= ulp(m.hi)/2.
    static int evaluate(const DoubleDouble &z, DoubleDouble &m);
    // m * 2^k as a double-double, both parts normal: for |k| below about 950.
    static DoubleDouble scaled(const DoubleDouble &m, int k);

private:
    // 2^(j/128) = powerTop_[j] + powerRest_[j], the top of 26 bits.
    static const double powerTop_[128], powerRest_[128];
};

}  // namespace rts6x

#endif
