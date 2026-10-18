// Spec: ISO C 7.12.4.5 cos, 7.12.4.6 sin, 7.12.4.7 tan and F.9.1.5-F.9.1.7: x = k pi/2 + r by
// ArgumentReduction, then r = a + t with a = j/64 from a table of sin a and cos a (S. Gal's and
// P. Tang's table method), sin t and cos t by Taylor series, combined by the addition formulas.
#ifndef RTS6X_TRIGONOMETRIC_H
#define RTS6X_TRIGONOMETRIC_H

#include "DoubleDouble.h"

namespace rts6x {

class Trigonometric {
public:
    // sin, cos, tan: +-0 kept (cos gives 1), an infinity a domain error (EDOM, NaN), a NaN itself.
    // sin x = evaluate(x, 0) and cos x = evaluate(x, 1), inline so that sin() makes one call.
    static double sine(double x) { return evaluate(x, 0); }
    static double cosine(double x) { return evaluate(x, 1); }
    static double tangent(double x);

    // sin r and cos r as double-doubles, |r| <= pi/4 + 2^-40, error about 2^-64 relative.
    static DoubleDouble sineKernel(const DoubleDouble &r);
    static DoubleDouble cosineKernel(const DoubleDouble &r);

private:
    // sin x (shift 0) or cos x (shift 1): below 2^13 by pi/256 and a table, in plain doubles.
    static double evaluate(double x, int shift);
    // The same by ArgumentReduction and the double-double kernels, for every finite x.
    static double slow(double x, int shift);
    // r split as a + t: returns j with a = j/64, t a double-double, |t| <= 1/128; r taken as |r|.
    static int split(const DoubleDouble &r, DoubleDouble &t);

    static const double sinHi_[53], sinLo_[53], cosHi_[53], cosLo_[53];
    // Row j: sin and cos of j pi/256, each a top of 26 bits and a rest.
    static const double rows_[512];
};

}  // namespace rts6x

#endif
