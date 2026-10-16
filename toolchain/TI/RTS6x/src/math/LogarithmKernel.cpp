// Spec: ISO C 7.12.6.7 log - the table-driven reduction of Logarithm.h. m r_j - 1 is exact as
// (mh r_j - 1) + ml r_j with mh m's top 45 bits; e ln2Hi - ln r_j is exact (multiples of 2^-42);
// u^2 by Dekker's product, so u - u^2/2 is a double-double, the rest of ln(1 + u) in double.

#include "Logarithm.h"
#include "MathBits.h"
#include "MathConstants.h"

namespace rts6x {

double Logarithm::kernel(double x, double &lo)
{
    union { double d; unsigned w[2]; } v;
    v.d = x;
    int e = (int)(v.w[1] >> 20) - 1023;
    if (e == -1023) {
        // Subnormal: its significand normalised, the exponent lowered to match.
        UnpackedFloat a(MathBits::of(x), FloatFormat::binary64());
        e = a.exponent() + 52;
        v.d = MathBits::from(a.significand());
    }
    v.w[1] = (v.w[1] & 0x000FFFFFu) | 0x3FF00000u;
    // j: m's fraction rounded to 1/128; at 128, m/2 is taken against r_0 = 1.
    int j = (int)(v.w[1] >> 13 & 0x7F) + (int)(v.w[1] >> 12 & 1);
    double m = v.d;
    if (j == 128) { m = 0.5 * m; e = e + 1; j = 0; v.d = m; }
    v.w[0] &= 0xFFFFFF00u;
    // m = v.d + (m - v.d): 45 bits times r_j's 8 is exact, and so is the rest's product.
    double u = (v.d * reciprocal_[j] - 1.0) + (m - v.d) * reciprocal_[j];
    double ed = (double)e;
    m = ed * MathConstants::ln2Hi + logHi_[j];
    // u = v.d + (u - v.d), v.d of 26 bits by Veltkamp; u^2 = sq + the exact rest.
    v.d = u * 134217729.0;
    v.d = v.d - (v.d - u);
    double sq = u * u;
    lo = ((v.d * v.d - sq) + 2.0 * v.d * (u - v.d)) + (u - v.d) * (u - v.d);
    // m + u by Knuth's two-sum, then - sq/2 by Dekker's fast one (|m + u| > sq).
    double s = m + u;
    double bv = s - m;
    lo = ((m - (s - bv)) + (u - bv)) + (ed * MathConstants::ln2Lo + logLo_[j]
         + (u * sq * ((0.3333333333333333 + u * -0.25) + sq * (0.2 + u * -0.16666666666666666)
            + sq * sq * ((0.14285714285714285 + u * -0.125) + sq * (0.1111111111111111 + u * -0.1)))
            - 0.5 * lo));
    m = s - 0.5 * sq;
    lo = ((s - m) - 0.5 * sq) + lo;
    return m;
}

}  // namespace rts6x
