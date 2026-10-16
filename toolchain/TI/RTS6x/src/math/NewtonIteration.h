// Spec: IEEE 754 binary64; Newton's method for 1/d (y <- y(2 - d y)) from a line on the significand
// alone and four steps, so with multiplications only: the C674x divides in software, and this needs
// no division at all.
#ifndef RTS6X_NEWTON_ITERATION_H
#define RTS6X_NEWTON_ITERATION_H

namespace rts6x {

class NewtonIteration {
public:
    // 1/d to within about 2 ulps, for 2^-1020 < |d| < 2^1020.
    static double reciprocal(double d);
    // 1/m at the middle of each of 128 bins of [1, 2), by m's top seven fraction bits: within 2^-8,
    // the first guess for a Newton step written out where it is used (ReciprocalTable.cpp).
    static const double seed_[128];
};

}  // namespace rts6x

#endif
