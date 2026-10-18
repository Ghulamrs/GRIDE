// Spec: IEEE 754 binary64 and binary32 (sign, biased exponent, fraction; subnormals, infinities,
// NaNs) and SPRAB89B 8.1: round to nearest even, no exceptions, a signalling NaN treated as quiet.
// Integer arithmetic only, so nothing here makes cpp11 call the floating helpers it implements.
#ifndef RTS6X_SOFT_FLOAT_H
#define RTS6X_SOFT_FLOAT_H

namespace rts6x {

class FloatFormat {
public:
    FloatFormat(int fractionBits, int exponentBits)
        : fractionBits_(fractionBits), exponentBits_(exponentBits), bias_((1 << (exponentBits - 1)) - 1) {}
    static FloatFormat binary64() { return FloatFormat(52, 11); }
    static FloatFormat binary32() { return FloatFormat(23, 8); }

    int fractionBits() const { return fractionBits_; }
    int precision() const { return fractionBits_ + 1; }
    int bias() const { return bias_; }
    int maxBiased() const { return (1 << exponentBits_) - 1; }
    unsigned long long signBit() const { return 1ull << (fractionBits_ + exponentBits_); }
    unsigned long long fractionMask() const { return (1ull << fractionBits_) - 1; }

    unsigned long long zero(bool negative) const { return negative ? signBit() : 0; }
    unsigned long long infinity(bool negative) const { return zero(negative) | ((unsigned long long)maxBiased() << fractionBits_); }
    // The default NaN: quiet, positive.
    unsigned long long nan() const { return infinity(false) | (1ull << (fractionBits_ - 1)); }

private:
    int fractionBits_, exponentBits_, bias_;
};

// A value taken apart: for Finite, value = significand x 2^exponent, the significand's top bit at
// fractionBits (subnormals normalised so), never zero.
class UnpackedFloat {
public:
    enum Kind { Zero, Finite, Infinite, NotANumber };
    UnpackedFloat(unsigned long long bits, const FloatFormat &format);

    Kind kind() const { return kind_; }
    bool negative() const { return negative_; }
    int exponent() const { return exponent_; }
    unsigned long long significand() const { return significand_; }

private:
    Kind kind_;
    bool negative_;
    int exponent_;
    unsigned long long significand_;
};

class FloatPacker {
public:
    // significand x 2^exponent, any width, with sticky for bits already lost below it, rounded to
    // the format: nearest, ties to even; subnormal, or infinity, where the exponent asks.
    static unsigned long long pack(const FloatFormat &format, bool negative, int exponent,
                                   unsigned long long significand, bool sticky);
    static int bitLength(unsigned long long x);
};

class FloatArithmetic {
public:
    static unsigned long long divide(const FloatFormat &format, unsigned long long a, unsigned long long b);
    // The same on doubles and floats: where divd.s and divf.s hand what is not normal over.
    static double divideBinary64(double x, double y);
    static float divideBinary32(float x, float y);
    // Truncated toward zero to a width-bit integer; beyond its range, the nearest end of it.
    static unsigned long long toUnsigned(const FloatFormat &format, unsigned long long bits, int width);
    static long long toSigned64(const FloatFormat &format, unsigned long long bits);
    static unsigned long long fromInteger(const FloatFormat &format, unsigned long long magnitude, bool negative);
    // binary64 to the integer toward zero, and to the nearest integer, halfway away from zero.
    static unsigned long long truncate(unsigned long long bits);
    static unsigned long long roundHalfAway(unsigned long long bits);

private:
    static unsigned long long quiet(const FloatFormat &format, unsigned long long bits)
    {
        return bits | (1ull << (format.fractionBits() - 1));
    }
};

// floor(n 2^shift / d), shift <= 55, and whether it leaves a remainder, for n and d in [2^52, 2^53):
// guessed by Newton's method for 1/d in binary64 (the C674x's MPYDP and SUBDP), then made exact by
// the integer remainder - the guess decides only how fast, never the bits.
class SignificandDivision {
public:
    SignificandDivision(unsigned long long n, unsigned long long d, int shift, int steps);
    unsigned long long quotient() const { return quotient_; }
    bool inexact() const { return inexact_; }

private:
    unsigned long long quotient_;
    bool inexact_;
};

// The bits of a double or a float, and back.
class FloatBits {
public:
    static unsigned long long of(double d) { union { double d; unsigned long long u; } x; x.d = d; return x.u; }
    static double toDouble(unsigned long long u) { union { double d; unsigned long long u; } x; x.u = u; return x.d; }
    static unsigned long long of(float f) { union { float f; unsigned u; } x; x.f = f; return x.u; }
    static float toFloat(unsigned long long u) { union { float f; unsigned u; } x; x.u = (unsigned)u; return x.f; }
};

}  // namespace rts6x

#endif
