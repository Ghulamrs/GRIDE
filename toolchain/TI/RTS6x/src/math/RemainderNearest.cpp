// Spec: ISO C99 7.12.10.2 remainder, 7.12.10.3 remquo and F.9.7.2, F.9.7.3, as the Microsoft runtime gives them
// (tests\math99): x - n y exactly, n nearest x / y and ties to even, the quotient n's low 31 bits with its sign;
// y zero or x infinite the quiet NaN and EDOM. Long division on the significands, a bit at a step, q's low bits kept.

#include "Remainder.h"
#include "BinaryScale.h"
#include "MathBits.h"
#include "MathError.h"

namespace {

using rts6x::MathBits;

// x = m 2^e with m in [2^52, 2^53): a finite nonzero x's significand made normal
void normalised(unsigned long long u, unsigned long long &m, int &e)
{
    int biased = MathBits::biased(u);
    m = u & MathBits::fractionMask();
    if (biased != 0) {
        m |= 1ull << 52;
        e = biased - 1075;
    } else {
        e = -1074;
        while (!(m >> 52)) {
            m <<= 1;
            e--;
        }
    }
}

}  // namespace

namespace rts6x {

double Remainder::nearest(double x, double y, int *quotient)
{
    *quotient = 0;
    unsigned long long ux = MathBits::of(x), uy = MathBits::of(y);
    if (MathBits::isNaN(ux) || MathBits::isNaN(uy)) return x + y;
    if (MathBits::isInfinite(ux) || MathBits::isZero(uy)) return MathError::domainC99();
    if (MathBits::isInfinite(uy) || MathBits::isZero(ux)) return x;
    unsigned long long mx, my;
    int ex, ey;
    normalised(ux, mx, ex);
    normalised(uy, my, ey);
    // |x| = q |y| + r 2^ey, by long division on the significands
    unsigned long long r = mx, q = 0;
    if (ex < ey) {
        // |x| < |y|: q is 0 and the remainder is |x|, in units of 2^ex
        r = 0;
    } else {
        for (int k = ex - ey; k > 0; k--) {
            q <<= 1;
            if (r >= my) {
                r -= my;
                q |= 1;
            }
            r <<= 1;
        }
        q <<= 1;
        if (r >= my) {
            r -= my;
            q |= 1;
        }
    }
    double magnitude;
    if (ex < ey) {
        // round: 2|x| against |y|, both exact as doubles here
        double ax = BinaryScale::magnitude(x), ay = BinaryScale::magnitude(y);
        if (ax > ay - ax) {
            magnitude = ax - ay;
            q = 1;
        } else {
            magnitude = ax;
        }
    } else {
        bool up = 2 * r > my || (2 * r == my && (q & 1));
        if (up) q++;
        double whole = up ? -(double)(long long)(my - r) : (double)(long long)r;
        magnitude = BinaryScale::scale(whole, ey);
    }
    int sign = MathBits::negative(ux) != MathBits::negative(uy) ? -1 : 1;
    *quotient = sign * (int)(q & 0x7FFFFFFF);
    return MathBits::negative(ux) ? -magnitude : magnitude;
}

}  // namespace rts6x
