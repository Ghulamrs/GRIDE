// Spec: ISO C 7.12.7.4 pow and F.9.4.4, every special case in its order: y = +-0 gives 1 for any
// x; x = +1 gives 1 for any y; a NaN otherwise gives a NaN; then the zeros, the infinities, and
// a negative x with a y that is not an integer (EDOM).

#include "Power.h"
#include "ExpKernel.h"
#include "Logarithm.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

double Power::raise(double x, double y)
{
    // x positive and normal, y normal below 2^64: none of the special cases, and the result normal
    // where |y ln x| < 704 - on the high words alone, then the kernels with no call between.
    union { double d; unsigned w[2]; } v;
    v.d = x;
    unsigned hx = v.w[1];
    v.d = y;
    if (hx - 0x00100000u < 0x7FE00000u && (v.w[1] & 0x7FFFFFFFu) - 0x00100000u < 0x43E00000u) {
        double lo, zh = Logarithm::kernel(x, lo);
        // The kernel leaves its Taylor tail in lo: hi + lo normalised first (Dekker's fast two-sum).
        double hi = zh + lo;
        lo = lo - (hi - zh);
        // z = y ln x: y * hi exactly by Dekker's product (both split by Veltkamp's 2^27 + 1).
        zh = y * hi;
        double yh = y * 134217729.0, hh = hi * 134217729.0;
        yh = yh - (yh - y);
        hh = hh - (hh - hi);
        double zl = (((yh * hh - zh) + yh * (hi - hh) + (y - yh) * hh) + (y - yh) * (hi - hh)) + y * lo;
        v.d = zh;
        if ((v.w[1] & 0x7FFFFFFFu) < 0x40860000u) {
            int k = ExpKernel::evaluate(zh, zl, hi, lo);
            v.d = hi + lo;
            v.w[1] += (unsigned)k << 20;
            return v.d;
        }
    }
    unsigned long long ux = MathBits::of(x), uy = MathBits::of(y);
    if (MathBits::isZero(uy) || ux == 0x3FF0000000000000ull) return 1.0;
    if (MathBits::isNaN(ux)) return x;
    if (MathBits::isNaN(uy)) return y;
    bool yNegative = MathBits::negative(uy), xNegative = MathBits::negative(ux);
    Parity py = parity(uy);
    if (MathBits::isInfinite(uy)) {
        unsigned long long ax = ux & MathBits::magnitudeMask();
        if (ax == 0x3FF0000000000000ull) return 1.0;
        // |x| < 1 against |x| > 1: +0 or +inf, the other way round for y = -inf.
        return (ax < 0x3FF0000000000000ull) != yNegative ? 0.0 : MathBits::infinity(false);
    }
    if (MathBits::isZero(ux)) {
        bool sign = xNegative && py == Odd;
        return yNegative ? MathError::overflow(sign) : MathBits::zero(sign);
    }
    if (MathBits::isInfinite(ux)) {
        bool sign = xNegative && py == Odd;
        return yNegative ? MathBits::zero(sign) : MathBits::infinity(sign);
    }
    if (xNegative && py == NotInteger) return MathError::domain();
    return finite(MathBits::absolute(x), y, xNegative && py == Odd);
}

}  // namespace rts6x
