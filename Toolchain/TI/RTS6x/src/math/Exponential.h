// Spec: ISO C 7.12.6.1 exp and F.9.3.1: e^x correctly to within an ulp; exp(+-0) is 1, exp(-inf)
// +0, exp(+inf) +inf; overflow to HUGE_VAL and underflow set ERANGE (7.12.1).
#ifndef RTS6X_EXPONENTIAL_H
#define RTS6X_EXPONENTIAL_H

namespace rts6x {

class Exponential {
public:
    static double natural(double x);    // exp
};

}  // namespace rts6x

#endif
