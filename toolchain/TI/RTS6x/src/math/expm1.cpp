// Spec: ISO C99 7.12.6.3 expm1 and F.9.3.3, with the errno the Microsoft runtime's UCRT sets (tests\math99). Below 0.35
// its Taylor series in double-double arithmetic, above it e^x less one; rounded once at the end.
#include <math.h>
#include "BinaryScale.h"
#include "ExpKernel.h"
#include "MathBits.h"
#include "MathError.h"
using rts6x::BinaryScale;
using rts6x::DoubleDouble;
using rts6x::ExpKernel;
using rts6x::MathBits;
extern "C" double expm1(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? x : -1.0;
    if (x > 709.8) return rts6x::MathError::overflow(false);
    if (x < -38.0) return -1.0;
    double a = fabs(x);
    if (a < 5.551115123125783e-17) return x;
    if (a < 0.35) {
        DoubleDouble term(x, 0.0), sum(x, 0.0);
        for (int n = 2; n < 40; n++) {
            term = term.times(x).over((double)n);
            sum = sum.plus(term);
            if (fabs(term.hi) < fabs(sum.hi) * 1e-34) break;
        }
        return sum.value();
    }
    int k;
    DoubleDouble m = ExpKernel::extended(DoubleDouble(x, 0.0), k);
    if (k > 950) return BinaryScale::scale(m.value(), k);
    return ExpKernel::scaled(m, k).plus(-1.0).value();
}
