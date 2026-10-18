// Spec: ISO C 6.3.1.4 - a floating value to an integer: the fraction discarded (toward zero), and
// an integer to floating: exact when it fits, else rounded (here to nearest even, SPRAB89B 8.1).
// Out of an integer's range C says nothing; these give the nearest end of the range, NaN 0.

#include "SoftFloat.h"

namespace rts6x {

unsigned long long FloatArithmetic::toUnsigned(const FloatFormat &format, unsigned long long bits, int width)
{
    UnpackedFloat v(bits, format);
    unsigned long long top = width == 64 ? ~0ull : (1ull << width) - 1;
    if (v.kind() == UnpackedFloat::Zero || v.kind() == UnpackedFloat::NotANumber) return 0;
    if (v.negative()) return 0;
    if (v.kind() == UnpackedFloat::Infinite) return top;
    int e = v.exponent();
    unsigned long long s = v.significand();
    if (e >= 0) return FloatPacker::bitLength(s) + e > width ? top : s << e;
    return -e >= 64 ? 0 : s >> -e;
}

long long FloatArithmetic::toSigned64(const FloatFormat &format, unsigned long long bits)
{
    UnpackedFloat v(bits, format);
    const unsigned long long limit = 1ull << 63;
    if (v.kind() == UnpackedFloat::Zero || v.kind() == UnpackedFloat::NotANumber) return 0;
    unsigned long long magnitude;
    if (v.kind() == UnpackedFloat::Infinite) magnitude = limit;
    else if (v.exponent() >= 0) magnitude = FloatPacker::bitLength(v.significand()) + v.exponent() > 63 ? limit : v.significand() << v.exponent();
    else magnitude = -v.exponent() >= 64 ? 0 : v.significand() >> -v.exponent();
    if (v.negative()) return (long long)(0ull - (magnitude > limit ? limit : magnitude));
    return magnitude >= limit ? (long long)(limit - 1) : (long long)magnitude;
}

unsigned long long FloatArithmetic::fromInteger(const FloatFormat &format, unsigned long long magnitude, bool negative)
{
    return magnitude == 0 ? format.zero(false) : FloatPacker::pack(format, negative, 0, magnitude, false);
}

}  // namespace rts6x
