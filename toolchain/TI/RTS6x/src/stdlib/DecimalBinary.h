// Spec: ISO C 7.20.1.3 and F.5, IEEE 754 5.12.2: w x 10^e (w < 2^64) rounded to binary64, from
// 5^e = 5^b 5^(28a) with 5^(28a) known to 64 bits, and taken only where that bound decides the
// rounding; the reasoning is in docs/DIVISION.md beside the division's.
#ifndef RTS6X_DECIMAL_BINARY_H
#define RTS6X_DECIMAL_BINARY_H

namespace rts6x {

class DecimalBinary {
public:
    // w x 10^e's bits, signed, when normal and decided by the bound; else false (BigNumber's turn).
    static bool toBinary64(unsigned long long w, int e, bool negative, unsigned long long &bits);

    struct Power {
        unsigned limbs[2];      // C, least significant limb first: 5^(28a) ~ C 2^k, C in [2^63, 2^64)
        int k;
        int exact;              // C 2^k is 5^(28a) itself
    };

private:
    // a 32 x 32-bit product's two words.
    static unsigned long long product(unsigned x, unsigned y) { return (unsigned long long)x * y; }
    static int bitLength(unsigned x);

    static const unsigned long long fives[28];
    static const Power powers[25];
};

}  // namespace rts6x

#endif
