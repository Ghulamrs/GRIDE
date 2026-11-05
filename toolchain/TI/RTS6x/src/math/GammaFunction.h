// Spec: ISO C99 7.12.8.4 tgamma and 7.12.8.3 lgamma, in double-double arithmetic: Stirling's series from 20 up, the
// argument raised to 20 by Gamma(y) = Gamma(y + n) / (y ... (y+n-1)), and below 1/2 the reflection
// pi / (sin(pi x) Gamma(1 - x)). lgamma within 0.2 of 1 and of 2, where it crosses zero, by its Taylor series there:
// lgamma(1 + e) = -gamma e + sum (-1)^k zeta(k)/k e^k, lgamma(2 + e) = (1 - gamma) e + sum (-1)^k (zeta(k) - 1)/k e^k.
#ifndef RTS6X_GAMMA_FUNCTION_H
#define RTS6X_GAMMA_FUNCTION_H

#include "DoubleDouble.h"

namespace rts6x {

class GammaFunction {
public:
    // Gamma(y) = m 2^k, k set and m returned, y >= 1/2.
    static DoubleDouble gamma(const DoubleDouble &y, int &k);
    // ln Gamma(y), y >= 1/2 away from 1 and 2.
    static DoubleDouble logGamma(const DoubleDouble &y);
    // sin(pi x) for x not an integer, |x| < 2^52: x less an even integer, exactly, then sin(pi r) for |r| <= 1/2.
    static DoubleDouble sinPi(double x);
    // lgamma(1 + e) and lgamma(2 + e), |e| < 0.2.
    static double nearOne(double e);
    static double nearTwo(double e);

private:
    // ln Gamma(z), z >= 20.
    static DoubleDouble stirling(const DoubleDouble &z);
    // The Taylor series whose first coefficient is c1 and whose others are table's, k = 2 .. 29.
    static double nearRoot(double e, const DoubleDouble &c1, const unsigned long long *table);
};

}  // namespace rts6x

#endif
