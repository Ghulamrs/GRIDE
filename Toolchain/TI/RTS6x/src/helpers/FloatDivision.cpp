// Spec: IEEE 754 6.1-6.2 and 7.2-7.3: x/y with NaN in giving NaN out, inf/inf and 0/0 the default
// NaN, x/0 an infinity, x/inf a zero, the sign the operands' exclusive or; else the exact quotient,
// rounded. The quotient's bits and remainder from SignificandDivision, its remainder the sticky bit.

#include "SoftFloat.h"

namespace rts6x {

unsigned long long FloatArithmetic::divide(const FloatFormat &format, unsigned long long a, unsigned long long b)
{
    UnpackedFloat x(a, format), y(b, format);
    bool negative = x.negative() != y.negative();
    if (x.kind() == UnpackedFloat::NotANumber) return quiet(format, a);
    if (y.kind() == UnpackedFloat::NotANumber) return quiet(format, b);
    if (x.kind() == UnpackedFloat::Infinite)
        return y.kind() == UnpackedFloat::Infinite ? format.nan() : format.infinity(negative);
    if (y.kind() == UnpackedFloat::Infinite) return format.zero(negative);
    if (y.kind() == UnpackedFloat::Zero)
        return x.kind() == UnpackedFloat::Zero ? format.nan() : format.infinity(negative);
    if (x.kind() == UnpackedFloat::Zero) return format.zero(negative);

    const int up = 52 - format.fractionBits(), shift = format.precision() + 2;
    SignificandDivision q(x.significand() << up, y.significand() << up, shift, up > 0 ? 3 : 4);
    return FloatPacker::pack(format, negative, x.exponent() - y.exponent() - shift, q.quotient(), q.inexact());
}

}  // namespace rts6x
