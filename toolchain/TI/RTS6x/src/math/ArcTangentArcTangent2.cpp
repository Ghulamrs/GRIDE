// Spec: ISO C 7.12.4.4 atan2 - with n <= d the magnitudes and c = j/32 within 1/64 of n/d,
// atan(n/d) = atan c + atan t, t = (n - c d)/(d + c n) (the addition formula with the quotient
// formed once); t by a reciprocal from Newton's method corrected by the exact remainder.

#include "ArcTangent.h"
#include "MathConstants.h"
#include "NewtonIteration.h"

namespace rts6x {

double ArcTangent::arcTangent2(double y, double x)
{
    union { double d; unsigned w[2]; } v;
    v.d = y;
    unsigned hy = v.w[1];
    v.d = x;
    unsigned hx = v.w[1];
    // Both normal and finite, their exponents at most 60 apart, on the high words alone; else edge().
    unsigned ay = hy & 0x7FFFFFFFu, ax = hx & 0x7FFFFFFFu;
    if (ay - 0x00100000u >= 0x7FE00000u || ax - 0x00100000u >= 0x7FE00000u
        || (ay >> 20) - (ax >> 20) + 60u > 120u) return edge(y, x);
    int ey = (int)(hy >> 20 & 0x7FF), ex = (int)(hx >> 20 & 0x7FF);
    int top = ey > ex ? ey : ex;
    // Both scaled by one power of two, the larger into [1, 2), the smaller still normal.
    v.w[1] = (hx & 0x000FFFFFu) | (unsigned)(ex - top + 1023) << 20;
    double d = v.d;
    v.d = y;
    v.w[1] = (hy & 0x000FFFFFu) | (unsigned)(ey - top + 1023) << 20;
    double n = v.d;
    int swap = n > d;
    if (swap) { n = d; d = v.d; }
    // j = nearest(32 n/d) from d's seed reciprocal (within 2^-8): j in 0..32.
    v.d = d;
    v.d = n * NewtonIteration::seed_[v.w[1] >> 13 & 127] * 32.0 + 6755399441055744.0;
    int j = (int)v.w[0];
    double c = (double)j * 0.03125;
    // n - c d and d + c n as double-doubles: d and n cut at 47 bits, so c times each part is exact.
    v.d = d;
    v.w[0] &= 0xFFFFFFC0u;
    double s = n - c * v.d, b = s - n;
    double nl = ((n - (s - b)) - (c * v.d + b)) - c * (d - v.d);
    v.d = n;
    v.w[0] &= 0xFFFFFFC0u;
    double dh = d + c * v.d;
    double dl = (c * v.d - (dh - d)) + c * (n - v.d);
    // 1/dh (dh in [1, 4)) from the seed and two Newton steps: within 2^-33, so th + tl = t(1 - e^2).
    v.d = dh;
    double r = NewtonIteration::seed_[v.w[1] >> 13 & 127] * ((v.w[1] >> 20) == 1023u ? 1.0 : 0.5);
    r = r + r * (1.0 - dh * r);
    r = r + r * (1.0 - dh * r);
    double th = s * r;
    // s - th dh exactly: th and dh split by Veltkamp's 2^27 + 1 (Dekker's product).
    double p = th * 134217729.0, q = dh * 134217729.0;
    p = p - (p - th);
    q = q - (q - dh);
    b = th * dh;
    double tl = ((((s - b) - (((p * q - b) + p * (dh - q) + (th - p) * q) + (th - p) * (dh - q))) + nl)
                 - th * dl) * r;
    // atan t - t to t^13 (|t| < 0.02: truncation below 2^-80 relative), tl (< 2^-32 t) times
    // atan's slope 1/(1 + t^2) to t^4; then atan c + atan t.
    b = th * th;
    s = atan32Hi_[j] + th;
    double lo = (th - (s - atan32Hi_[j])) + (atan32Lo_[j]
                + (tl - tl * b * (1.0 - b) + th * b * (-0.3333333333333333 + b * (0.2 + b * (-0.14285714285714285
                   + b * (0.1111111111111111 + b * (-0.09090909090909091 + b * 0.07692307692307693)))))));
    unsigned xNegative = hx >> 31;
    if (swap | xNegative) {
        // pi - a, pi/2 - a or pi/2 + a: the constant's head plus +-s exactly (fast two-sum).
        double kh = swap ? MathConstants::halfPiHi : MathConstants::piHi;
        double kl = swap ? MathConstants::halfPiLo : MathConstants::piLo;
        if ((unsigned)swap != xNegative) { s = -s; lo = -lo; }
        b = kh + s;
        lo = (s - (b - kh)) + (kl + lo);
        s = b;
    }
    v.d = s + lo;
    v.w[1] ^= hy & 0x80000000u;
    return v.d;
}

}  // namespace rts6x
