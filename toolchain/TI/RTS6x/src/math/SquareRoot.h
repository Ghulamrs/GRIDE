// Spec: ISO C 7.12.7.5 sqrt and F.9.4.5; IEEE 754 5.4.1: the square root correctly rounded - a
// root within an ulp by Goldschmidt's iteration, then its exact remainder (Dekker) says which of
// three neighbours it is; near a halfway point the integer root settles it exactly.
#ifndef RTS6X_SQUARE_ROOT_H
#define RTS6X_SQUARE_ROOT_H

namespace rts6x {

class SquareRoot {
public:
    // sqrt: -0 is -0, +inf +inf, a NaN itself; below zero a domain error (EDOM, NaN).
    static double root(double x);
    // 1/sqrt(w) to within about 2 ulps, for 2^-1020 < w < 2^1020: no division, no rounding asked.
    static double inverse(double w);

private:
    // sqrt of x > 0 finite by an exact integer root: for a subnormal x, or a root near a half.
    static double exact(double x);
    // 1/sqrt(m) ~ base_[i] + slope_[i] * m within 2^-14, i = m's bin of [1, 4) (SqrtTable.cpp).
    static const double base_[64], slope_[64];
};

}  // namespace rts6x

#endif
