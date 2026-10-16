// Spec: IEEE 754 4.3.1 and 7: round to nearest, ties to even, on the exact result; gradual
// underflow to subnormals; overflow to infinity. One packer for every helper that rounds.

#include "SoftFloat.h"

namespace rts6x {

UnpackedFloat::UnpackedFloat(unsigned long long bits, const FloatFormat &format)
    : kind_(Zero), negative_((bits & format.signBit()) != 0), exponent_(0), significand_(0)
{
    int biased = (int)((bits >> format.fractionBits()) & (unsigned long long)format.maxBiased());
    unsigned long long fraction = bits & format.fractionMask();
    if (biased == format.maxBiased()) { kind_ = fraction ? NotANumber : Infinite; return; }
    if (biased == 0 && fraction == 0) return;
    kind_ = Finite;
    if (biased == 0) {
        significand_ = fraction;
        exponent_ = 1 - format.bias() - format.fractionBits();
        while ((significand_ >> format.fractionBits()) == 0) { significand_ <<= 1; exponent_--; }
    } else {
        significand_ = fraction | (1ull << format.fractionBits());
        exponent_ = biased - format.bias() - format.fractionBits();
    }
}

int FloatPacker::bitLength(unsigned long long x)
{
    // Halving the width six times: a fixed few steps where one bit a turn took up to 64.
    int n = 0;
    if (x >> 32) { n += 32; x >>= 32; }
    unsigned w = (unsigned)x;
    if (w >> 16) { n += 16; w >>= 16; }
    if (w >> 8) { n += 8; w >>= 8; }
    if (w >> 4) { n += 4; w >>= 4; }
    if (w >> 2) { n += 2; w >>= 2; }
    if (w >> 1) { n += 1; w >>= 1; }
    return n + (int)w;
}

unsigned long long FloatPacker::pack(const FloatFormat &format, bool negative, int exponent,
                                     unsigned long long significand, bool sticky)
{
    const int p = format.precision(), target = p + 2;
    if (significand == 0) return format.zero(negative);
    // Exactly p + 2 bits: the result's p, a guard bit and a round bit; the rest folded into sticky.
    int n = bitLength(significand);
    if (n > target) {
        int shift = n - target;
        sticky = sticky || (significand & ((1ull << shift) - 1)) != 0;
        significand >>= shift;
        exponent += shift;
    } else {
        significand <<= target - n;
        exponent -= target - n;
    }
    int biased = exponent + p + 1 + format.bias();
    if (biased >= format.maxBiased()) return format.infinity(negative);
    if (biased <= 0) {
        int shift = 1 - biased;
        if (shift >= 64) { sticky = sticky || significand != 0; significand = 0; }
        else { sticky = sticky || (significand & ((1ull << shift) - 1)) != 0; significand >>= shift; }
        biased = 0;
    }
    bool guard = ((significand >> 1) & 1) != 0, rest = (significand & 1) != 0 || sticky;
    significand >>= 2;
    if (guard && (rest || (significand & 1) != 0)) significand++;
    if (biased == 0) {
        if ((significand >> (p - 1)) != 0) biased = 1;
    } else if ((significand >> p) != 0) {
        significand >>= 1;
        if (++biased >= format.maxBiased()) return format.infinity(negative);
    }
    return format.zero(negative) | ((unsigned long long)biased << format.fractionBits()) | (significand & format.fractionMask());
}

}  // namespace rts6x
