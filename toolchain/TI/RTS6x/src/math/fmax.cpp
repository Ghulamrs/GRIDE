// Spec: ISO C99 7.12.12.2 fmax and F.9.9.2, as the Microsoft runtime gives it (tests\math99): a NaN argument passed
// over, and of two zeros +0.
#include <math.h>
#include "MathBits.h"
using rts6x::MathBits;
extern "C" double fmax(double x, double y)
{
    if (x != x) return y;
    if (y != y) return x;
    if (x == y) return MathBits::from(MathBits::of(x) & MathBits::of(y));
    return x > y ? x : y;
}
