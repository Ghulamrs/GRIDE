// Spec: ISO C99 7.12.10.2 remainder and F.9.7.2. The Microsoft runtime, unlike its remquo, answers an infinite x with a
// NaN of x's sign and EDOM, y a NaN or not (tests\math99).
#include <errno.h>
#include <math.h>
#include "MathBits.h"
#include "MathError.h"
#include "Remainder.h"
using rts6x::MathBits;
extern "C" double remainder(double x, double y)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isInfinite(u)) {
        rts6x::ErrorNumber::set(EDOM);
        return MathBits::from(0x7FF8000000000000ull | (u & MathBits::signBit()));
    }
    int q;
    return rts6x::Remainder::nearest(x, y, &q);
}
