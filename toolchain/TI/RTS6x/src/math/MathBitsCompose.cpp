// Spec: IEEE 754 4.3.1 and 7.4, 7.5: (hi + lo) * 2^k rounded once to nearest, ties to even, with
// gradual underflow; ISO C 7.12.1/5 and /6: overflow and underflow are range errors (ERANGE).

#include "MathBits.h"
#include "DoubleDouble.h"
#include "MathError.h"

namespace rts6x {

double MathBits::compose(const DoubleDouble &m, int k, bool negative)
{
    double v = m.hi + m.lo;
    unsigned long long u = of(v);
    int e = biased(u) + k;
    if (e > 2046) return MathError::overflow(negative);
    if (e >= 1) return from((u & ~(0x7FFull << 52)) | ((unsigned long long)e << 52) | (negative ? signBit() : 0));
    // Subnormal or zero: hi's 53 bits and ten more below them, lo folded in exactly or as sticky.
    UnpackedFloat h(of(m.hi), FloatFormat::binary64()), l(of(m.lo), FloatFormat::binary64());
    unsigned long long w = h.significand() << 10;
    int base = h.exponent() - 10;
    bool sticky = false;
    if (l.kind() == UnpackedFloat::Finite) {
        int shift = l.exponent() - base;
        unsigned long long part = 0;
        bool rest = false;
        if (shift >= 0) part = shift < 10 ? l.significand() << shift : 0;
        else if (shift > -64) { part = l.significand() >> -shift; rest = (l.significand() & ((1ull << -shift) - 1)) != 0; }
        else rest = true;
        if (!l.negative()) { w += part; sticky = rest; }
        else { w -= part; if (rest) { w -= 1; sticky = true; } }
    }
    MathError::range();
    return from(FloatPacker::pack(FloatFormat::binary64(), negative, base + k, w, sticky));
}

}  // namespace rts6x
