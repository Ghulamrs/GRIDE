// Spec: ISO C 7.12.6.1 exp - Cody and Waite's reduction with a table of 2^(j/128): e^z = 2^k
// 2^(j/128) e^a e^c, a = z - n ln2Hi/128 exact, c the rest (|c| < 2^-24); e^a - 1 - a by Taylor to
// a^6 (truncation below 2^-72 for |a| <= ln2/256), in Estrin's order for a short chain.

#include "ExpKernel.h"
#include "MathBits.h"
#include "MathConstants.h"

namespace rts6x {

int ExpKernel::evaluate(double zh, double zl, double &mh, double &ml)
{
    // n = nearest(z 128/ln2) by the 1.5 * 2^52 shift: its low word is n, |n| < 2^18.
    union { double d; int w[2]; } shifted;
    shifted.d = zh * MathConstants::oneTwentyEightByLn2 + 6755399441055744.0;
    int n = shifted.w[0];
    double nd = shifted.d - 6755399441055744.0;
    // n * ln2By128Hi is exact (18 + 35 bits), and so is the subtraction (Sterbenz).
    double a = zh - nd * MathConstants::ln2By128Hi;
    double c = zl - nd * MathConstants::ln2By128Lo;
    double a2 = a * a;
    double q = a2 * ((0.5 + a * 0.16666666666666666) + a2 * (0.041666666666666664 + a * 0.008333333333333333
               + a2 * 0.001388888888888889));
    // e^c - 1 = c + c^2/2 (c^3/6 < 2^-75), and e^(a + c) - 1 - a = q + (e^c - 1)(1 + a + q).
    double b = q + (c + 0.5 * c * c) * (1.0 + (a + q));
    // a = ah + al, ah a multiple of 2^-33 below 2^-8: 25 bits, so top * ah is exact.
    double ah = (a + 786432.0) - 786432.0;
    int j = n & 127;
    double top = powerTop_[j], rest = powerRest_[j];
    double p = top * ah;
    mh = top + p;
    // top >= |p|: the error of mh is exact (Dekker's fast two-sum).
    ml = (p - (mh - top)) + (rest + (top * ((a - ah) + b) + rest * (a + b)));
    return n >> 7;
}

int ExpKernel::evaluate(const DoubleDouble &z, DoubleDouble &m)
{
    double h, l;
    int k = evaluate(z.hi, z.lo, h, l);
    m = DoubleDouble::quickSum(h, l);
    return k;
}

DoubleDouble ExpKernel::scaled(const DoubleDouble &m, int k)
{
    double f = MathBits::powerOfTwo(k);
    return DoubleDouble(m.hi * f, m.lo * f);
}

}  // namespace rts6x
