// Spec: ISO C 7.12.4.5-7.12.4.7 (cos, sin, tan for every finite argument): x = k pi/2 + r, |r|
// <= pi/4. Below 2^20, Cody and Waite's subtraction of k times pi/2 in four parts; otherwise, or
// where r is too small to trust that, x times the bits of 2/pi in integers (Payne and Hanek).
#ifndef RTS6X_ARGUMENT_REDUCTION_H
#define RTS6X_ARGUMENT_REDUCTION_H

#include "DoubleDouble.h"

namespace rts6x {

class ArgumentReduction {
public:
    // x finite: k mod 4 returned, r a double-double, |r| <= pi/4 (+2^-50), relative error < 2^-70.
    static int reduce(double x, DoubleDouble &r);

private:
    // |x| = ax, normal, any size: k mod 4 and r from 192 bits of the product x * 2/pi.
    static int exact(double ax, DoubleDouble &r);
    // The 32 bits of floor(2^1216 * 2/pi) from bit position pos upward (bit 0 the least).
    static unsigned window(int pos);

    static const unsigned twoOverPi_[38];
};

}  // namespace rts6x

#endif
