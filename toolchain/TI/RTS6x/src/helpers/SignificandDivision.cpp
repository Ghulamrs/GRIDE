// Spec: IEEE 754 7.2-7.3 needs the exact quotient's leading bits and remainder; Newton's method for
// 1/b (y <- y(2 - b y), the error squaring each step; Knuth, TAOCP vol. 2, 4.3.3) guesses them, and
// the remainder n 2^shift - q d, exact in integers, corrects the guess until 0 <= r < d.

#include "SoftFloat.h"

namespace rts6x {

SignificandDivision::SignificandDivision(unsigned long long n, unsigned long long d, int shift, int steps)
{
    const unsigned long long fraction = (1ull << 52) - 1, one = 1023ull << 52;
    double a = FloatBits::toDouble((n & fraction) | one), b = FloatBits::toDouble((d & fraction) | one);
    // 24/17 - 8b/17, the line through (1, 1) and (2, 1/2) less its mean error: within 1/17 of 1/b.
    double y = 1.411764705882353 - 0.47058823529411764 * b;
    for (int i = steps; i > 0; i--) y = y * (2.0 - b * y);
    // a y is a/b in (1/2, 2) to a few ulps (four steps); its significand, moved to the quotient's scale.
    unsigned long long u = FloatBits::of(a * y);
    int up = (int)(u >> 52) - 1023 - 52 + shift;
    unsigned long long q = (u & fraction) | (1ull << 52);
    q = up >= 0 ? q << up : q >> -up;
    // |q - guess| d < 2^63, so the remainder's low 64 bits, taken as signed, are all of it.
    long long r = (long long)((n << shift) - q * d);
    double rough = (double)(int)(r >> 32) * 4294967296.0 + (double)(unsigned)r;
    // r / d = r (1/b) 2^-52, to within one; then whole steps of d to 0 <= r < d.
    int c = (int)(rough * y * 2.220446049250313e-16);
    q += (unsigned long long)(long long)c;
    r -= (long long)c * (long long)d;
    while (r < 0) { q--; r += (long long)d; }
    while (r >= (long long)d) { q++; r -= (long long)d; }
    quotient_ = q;
    inexact_ = r != 0;
}

}  // namespace rts6x
