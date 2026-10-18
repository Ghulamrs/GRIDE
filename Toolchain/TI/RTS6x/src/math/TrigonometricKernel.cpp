// Spec: ISO C 7.12.4.5 and 7.12.4.6 - sin(a + t) = sin a + (cos a sin t + sin a (cos t - 1)) and
// cos(a + t) = cos a + (cos a (cos t - 1) - sin a sin t), the leading products exact; sin t - t to
// t^7 and cos t - 1 to t^6, for |t| <= 1/128 truncated below 2^-71.

#include "Trigonometric.h"

namespace rts6x {

int Trigonometric::split(const DoubleDouble &r, DoubleDouble &t)
{
    DoubleDouble a = r.hi < 0 ? r.negated() : r;
    int j = (int)(a.hi * 64.0 + 0.5);
    // a.hi - j/64 is exact: the two are within 1/128 of each other.
    t = DoubleDouble::sum(a.hi - j * 0.015625, a.lo);
    return j;
}

DoubleDouble Trigonometric::sineKernel(const DoubleDouble &r)
{
    DoubleDouble t;
    int j = split(r, t);
    double t2 = t.hi * t.hi;
    double sinTail = t.hi * t2 * (-0.16666666666666666 + t2 * (0.008333333333333333 + t2 * -0.0001984126984126984));
    double cosTail = t2 * (-0.5 + t2 * (0.041666666666666664 + t2 * -0.001388888888888889));
    double sh = sinHi_[j], ch = cosHi_[j];
    DoubleDouble p = DoubleDouble::product(ch, t.hi);
    DoubleDouble s = DoubleDouble::sum(sh, p.hi);
    double lo = s.lo + p.lo + sinLo_[j] + ch * t.lo + cosLo_[j] * t.hi
                + (sh * (cosTail - t.hi * t.lo) + ch * sinTail);
    DoubleDouble v = DoubleDouble::quickSum(s.hi, lo);
    return r.hi < 0 ? v.negated() : v;
}

DoubleDouble Trigonometric::cosineKernel(const DoubleDouble &r)
{
    DoubleDouble t;
    int j = split(r, t);
    double t2 = t.hi * t.hi;
    double sinTail = t.hi * t2 * (-0.16666666666666666 + t2 * (0.008333333333333333 + t2 * -0.0001984126984126984));
    double cosTail = t2 * (-0.5 + t2 * (0.041666666666666664 + t2 * -0.001388888888888889));
    double sh = sinHi_[j], ch = cosHi_[j];
    DoubleDouble p = DoubleDouble::product(sh, t.hi);
    DoubleDouble s = DoubleDouble::sum(ch, -p.hi);
    double lo = s.lo - p.lo + cosLo_[j] - sh * t.lo - sinLo_[j] * t.hi
                + (ch * (cosTail - t.hi * t.lo) - sh * sinTail);
    return DoubleDouble::quickSum(s.hi, lo);
}

}  // namespace rts6x
