// Spec: ISO C 7.12.6.4 frexp, 7.12.6.6 ldexp, 7.12.7.2 fabs, F.9.3.4, F.9.3.6, F.9.4.2: the
// binary exponent taken out or put in on the bits, so subnormals are exact; ldexp rounds once.
#ifndef RTS6X_BINARY_SCALE_H
#define RTS6X_BINARY_SCALE_H

namespace rts6x {

class BinaryScale {
public:
    // frexp: x = fraction * 2^*exponent, 0.5 <= |fraction| < 1; zero, inf, NaN returned, *exponent 0.
    static double fraction(double x, int *exponent);
    // ldexp: x * 2^n rounded once; ERANGE where it overflows or underflows to zero.
    static double scale(double x, int n);
    // fabs: the sign bit cleared.
    static double magnitude(double x);
};

}  // namespace rts6x

#endif
