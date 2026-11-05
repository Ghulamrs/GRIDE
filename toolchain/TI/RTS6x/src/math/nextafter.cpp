// Spec: ISO C99 7.12.11.3 nextafter and F.9.8.3, as the Microsoft runtime gives it (tests\math99): y when x == y, and
// ERANGE for a result that is infinite, subnormal or zero from a finite x. Compared on the bits, so a subnormal x has
// a next value on the C674x too, whose arithmetic reads it as zero.
#include <errno.h>
#include <math.h>
#include "MathBits.h"
#include "MathError.h"
using rts6x::ErrorNumber;
using rts6x::MathBits;
extern "C" double nextafter(double x, double y)
{
    unsigned long long ux = MathBits::of(x), uy = MathBits::of(y);
    if (MathBits::isNaN(ux) || MathBits::isNaN(uy)) return x + y;
    if (ux == uy || (MathBits::isZero(ux) && MathBits::isZero(uy))) return y;
    if (MathBits::isZero(ux)) {
        ErrorNumber::set(ERANGE);
        return MathBits::from((uy & MathBits::signBit()) | 1);
    }
    bool away = MathBits::negative(ux) == MathBits::negative(uy) &&
                (ux & MathBits::magnitudeMask()) < (uy & MathBits::magnitudeMask());
    unsigned long long v = away ? ux + 1 : ux - 1;
    if (MathBits::isFinite(ux) && (!MathBits::isFinite(v) || MathBits::biased(v) == 0)) ErrorNumber::set(ERANGE);
    return MathBits::from(v);
}
