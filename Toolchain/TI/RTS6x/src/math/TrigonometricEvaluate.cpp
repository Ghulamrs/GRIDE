// Spec: ISO C 7.12.4.5 cos, 7.12.4.6 sin, F.9.1.5-6 - Cody and Waite by pi/256: n = nearest(x 256/pi),
// t = x - n (p1 + p2 + p3 + p4) (33-bit parts, n < 2^20, n p1..p3 exact); a = (n mod 128) pi/256,
// f(a + t) = f(a) cos u + g(a) sin u for (f, g) = (sin, cos), u = t, or (cos, sin), u = -t.

#include "Trigonometric.h"
#include "MathBits.h"
#include "MathConstants.h"
#include "MathError.h"

namespace rts6x {

// Few named doubles and long expressions: cpp11 keeps four doubles in saved registers and the
// rest in its frame, while an expression's temporaries stay in registers.
double Trigonometric::evaluate(double x, int shift)
{
    union { double d; unsigned w[2]; } v;
    v.d = x;
    // 2^-27 <= |x| < 2^13 on the high word alone; else the special values, a tiny x (sin x rounds
    // to x and cos x to 1 below 2^-27) and the double-double way for a large one.
    if ((v.w[1] & 0x7FFFFFFFu) - 0x3E400000u >= 0x02800000u) {
        unsigned long long u = MathBits::of(x);
        if (MathBits::isNaN(u)) return x;
        if (MathBits::isInfinite(u)) return MathError::domain();
        if (MathBits::below(x, -27)) return shift ? 1.0 : x;
        return slow(x, shift);
    }
    unsigned negative = shift ? 0u : v.w[1] >> 31;
    v.w[1] &= 0x7FFFFFFFu;
    double t = v.d;
    v.d = t * MathConstants::twoFiftySixByPi + 6755399441055744.0;
    int n = (int)v.w[0];
    double kd = v.d - 6755399441055744.0;
    // x - n p1 is exact (Sterbenz); t = y - n p2 by Knuth's two-sum, rl the rest of n pi/256.
    double y = t - kd * MathConstants::piBy256Part1;
    double rh = y - kd * MathConstants::piBy256Part2;
    t = rh - y;
    double rl = ((y - (rh - t)) - (kd * MathConstants::piBy256Part2 + t))
                - (kd * MathConstants::piBy256Part3 + kd * MathConstants::piBy256Part4);
    int j = n & 127, q = (n >> 7) + shift, odd = q & 1;
    // rl's own rounding is below 2^-108: where the answer is t itself (a sine, j = 0), a t below
    // 2^-37 near a multiple of pi asks more than that, and the double-double reduction answers.
    if (j == 0 && !odd) {
        v.d = rh;
        if ((v.w[1] & 0x7FFFFFFFu) < 0x3DA00000u) return slow(x, shift);
    }
    negative ^= (unsigned)q >> 1 & 1;
    const double *f = rows_ + 4 * j + 2 * odd, *g = rows_ + 4 * j + 2 - 2 * odd;
    t = odd ? -rh : rh;
    if (odd) rl = -rl;
    // t's top 26 bits by Veltkamp's 2^27 + 1, so g[0] * y is exact; then f + g[0] y exactly as rh
    // + the error (Dekker's fast two-sum: |f| >= |g[0] y|, or f is 0).
    y = t * 134217729.0;
    y = y - (y - t);
    rh = f[0] + g[0] * y;
    kd = t * t;
    // cos u - 1 to u^6 and sin u - u to u^7 (|u| < 0.0062: truncation below 2^-74), rl to first order.
    v.d = rh + ((g[0] * y - (rh - f[0])) + (f[1] + ((g[0] * ((t - y) + rl) + g[1] * t)
          + ((f[0] + f[1]) * (kd * (-0.5 + kd * (0.041666666666666664 + kd * -0.001388888888888889)) - t * rl)
             + (g[0] + g[1]) * (kd * (t * (-0.16666666666666666 + kd * (0.008333333333333333
                + kd * -0.0001984126984126984)) - 0.5 * rl))))));
    v.w[1] ^= negative << 31;
    return v.d;
}

}  // namespace rts6x
