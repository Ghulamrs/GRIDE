// Spec: ISO C 7.12.9.6 round and 7.12.9.8 trunc, binary64 to binary64, exact; the same arithmetic
// SPRAB89B's __c6xabi_nround and __c6xabi_trunc are built on.

#include "IntegralPart.h"
#include "MathBits.h"

namespace rts6x {

double IntegralPart::towardZero(double x) { return MathBits::from(FloatArithmetic::truncate(MathBits::of(x))); }

double IntegralPart::nearestAway(double x) { return MathBits::from(FloatArithmetic::roundHalfAway(MathBits::of(x))); }

}  // namespace rts6x
