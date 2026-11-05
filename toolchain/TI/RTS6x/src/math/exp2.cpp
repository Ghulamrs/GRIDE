// Spec: ISO C99 7.12.6.2 exp2 and F.9.3.2, with the errno the Microsoft runtime's UCRT sets (tests\math99): an
// overflow HUGE_VAL and ERANGE, an underflow to zero no errno. 2^x = 2^n e^(f ln 2), n the integer nearest x, in
// double-double arithmetic rounded once at the end: within an ulp, nearly always half.
#include <errno.h>
#include <math.h>
#include "BinaryScale.h"
#include "ExpKernel.h"
#include "ExtendedConstants.h"
#include "MathBits.h"
#include "MathError.h"
using rts6x::BinaryScale;
using rts6x::DoubleDouble;
using rts6x::ExpKernel;
using rts6x::ExtendedConstants;
using rts6x::MathBits;
extern "C" double exp2(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? x : 0.0;
    if (x >= 1024.0) return rts6x::MathError::overflow(false);
    if (x < -1080.0) return 0.0;
    double n = rint(x), f = x - n;
    DoubleDouble lnTwo = ExtendedConstants::lnTwo();
    DoubleDouble z = DoubleDouble::product(f, lnTwo.hi).plus(f * lnTwo.lo);
    int k;
    DoubleDouble m = ExpKernel::extended(z, k);
    int saved = errno;
    double r = BinaryScale::scale(m.value(), k + (int)n);
    if (r == 0.0) errno = saved;
    return r;
}
