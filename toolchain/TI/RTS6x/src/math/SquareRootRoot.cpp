// Spec: ISO C 7.12.7.5 sqrt, F.9.4.5, IEEE 754 5.4.1 - correctly rounded. x = S 2^E, E even, m = S
// 2^-52 in [1, 4): 1/sqrt(m) from a table line and two Goldschmidt steps give sqrt(m) within an
// ulp; m - g^2 picks the neighbour, or T = floor(sqrt(S 2^52)) in integers settles it exactly.

#include "SquareRoot.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double SquareRoot::exact(double x)
{
    unsigned long long u = MathBits::of(x);
    unsigned top = (unsigned)(u >> 32);
    unsigned long long s;
    int biased;
    if (top >= 0x00100000u) {
        biased = (int)(top >> 20);
        s = (u & MathBits::fractionMask()) | (1ull << 52);
    } else {
        // Subnormal: the significand shifted up to 53 bits, the exponent down to match.
        s = u;
        biased = 1;
        while (!(s >> 52)) { s <<= 1; biased--; }
    }
    // x = s 2^(biased - 1075); an odd exponent gives a bit to s, so m = s 2^-52 lies in [1, 4).
    int odd = !(biased & 1);
    s <<= odd;
    double m = MathBits::from(((s >> odd) & MathBits::fractionMask()) | (unsigned long long)(1023 + odd) << 52);
    int i = (odd << 5) | (int)((s >> (47 + odd)) & 31);
    double y = base_[i] + slope_[i] * m;
    double g = m * y, h = 0.5 * y;
    double r = 0.5 - g * h;
    g = g + g * r;
    h = h + h * r;
    g = g + g * (0.5 - g * h);
    // t = g 2^52 from g's bits (g in [1, 2]): within a unit or two of T.
    unsigned long long gb = MathBits::of(g);
    unsigned long long t = (gb & MathBits::fractionMask()) | (1ull << 52);
    if (gb >= 0x4000000000000000ull) t = (1ull << 53) - 1;
    // s 2^52 - t^2 is small for a t this close, so its low 64 bits are all of it.
    long long rest = (long long)((s << 52) - t * t);
    while (rest < 0) { rest += (long long)(2 * t - 1); t--; }
    while (rest > (long long)(2 * t)) { rest -= (long long)(2 * t + 1); t++; }
    if (rest > (long long)t) t++;
    // x = m 2^(biased - 1023 - odd): the root's exponent is half that, its significand t 2^-52.
    int e = (biased - 1023 - odd) / 2 + 1023;
    if (t >> 53) { t >>= 1; e++; }
    return MathBits::from((t & MathBits::fractionMask()) | ((unsigned long long)e << 52));
}

double SquareRoot::root(double x)
{
    union { double d; unsigned w[2]; } v;
    v.d = x;
    unsigned top = v.w[1];
    if (top - 0x00100000u >= 0x7FE00000u) {
        // Not positive, normal and finite: the special values here, a subnormal exactly.
        unsigned long long u = MathBits::of(x);
        if (MathBits::isZero(u) || MathBits::isNaN(u)) return x;
        if (MathBits::negative(u)) return MathError::domain();
        if (MathBits::isInfinite(u)) return x;
        return exact(x);
    }
    // x = m 2^(2h), m in [1, 4): the exponent's last bit (biased even) moves into m.
    int odd = !(top >> 20 & 1);
    int i = (odd << 5) | (int)(top >> 15 & 31);
    v.w[1] = (top & 0x000FFFFFu) | (unsigned)(1023 + odd) << 20;
    double m = v.d;
    double y = base_[i] + slope_[i] * m;
    // Goldschmidt: g -> sqrt(m), h -> 1/(2 sqrt(m)), the error e becoming 1.5e^2 a step.
    double g = m * y, h = 0.5 * y;
    y = 0.5 - g * h;
    g = g + g * y;
    h = h + h * y;
    g = g + g * (0.5 - g * h);
    // m - g^2 exactly (g^2 by Dekker's product, g split by Veltkamp's 2^27 + 1), and so how far
    // sqrt(m) lies from g in units of 2^-52: the nearer neighbour is plain unless near a half.
    y = g * 134217729.0;
    y = y - (y - g);
    double p = g * g;
    double d = ((m - p) - (((y * y - p) + 2.0 * y * (g - y)) + (g - y) * (g - y))) * h * 4503599627370496.0;
    if (d > 0.5001) {
        if (d > 1.4999) return exact(x);
        g = g + 2.220446049250313e-16;
    } else if (d < -0.5001) {
        if (d < -1.4999) return exact(x);
        g = g - 2.220446049250313e-16;
    } else if (d > 0.4999 || d < -0.4999) {
        return exact(x);
    }
    v.d = g;
    v.w[1] += (unsigned)(((int)(top >> 20) - 1023 - odd) / 2) << 20;
    return v.d;
}

}  // namespace rts6x
