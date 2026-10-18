// Spec: IEEE 754 7.2-7.3 - the general entries of binary64 and binary32 division, by the bits: the
// paths divd.s and divf.s hand over to when an operand or the result is not a normal number.

#include "SoftFloat.h"

namespace rts6x {

double FloatArithmetic::divideBinary64(double x, double y)
{
    return FloatBits::toDouble(divide(FloatFormat::binary64(), FloatBits::of(x), FloatBits::of(y)));
}

float FloatArithmetic::divideBinary32(float x, float y)
{
    unsigned long long q = divide(FloatFormat::binary32(), FloatBits::of(x), FloatBits::of(y));
    return FloatBits::toFloat(q);
}

}  // namespace rts6x
