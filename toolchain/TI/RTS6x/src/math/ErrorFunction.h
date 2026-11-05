// Spec: ISO C99 7.12.8.1 erf and 7.12.8.2 erfc, in double-double arithmetic:
//   below 2, (2/sqrt(pi)) a e^(-a^2) sum (2a^2)^n / (1 3 5 ... (2n+1)), whose terms are all positive;
//   from 2, erfc by Laplace's continued fraction e^(-a^2) / (sqrt(pi) (a + (1/2)/(a + 1/(a + ...)))).
#ifndef RTS6X_ERROR_FUNCTION_H
#define RTS6X_ERROR_FUNCTION_H

#include "DoubleDouble.h"

namespace rts6x {

class ErrorFunction {
public:
    // erf(a), 0 <= a < 6.
    static DoubleDouble series(double a);
    // erfc(a) = m 2^k, k set and m returned, 2 <= a <= 27.3.
    static DoubleDouble fraction(double a, int &k);
};

}  // namespace rts6x

#endif
