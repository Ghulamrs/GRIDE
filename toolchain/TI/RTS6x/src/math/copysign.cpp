// Spec: ISO C99 7.12.11.1 copysign and F.9.8.1 - x's magnitude with y's sign, on the bits.
#include <math.h>
#include "MathBits.h"
using rts6x::MathBits;
extern "C" double copysign(double x, double y)
{
    return MathBits::from((MathBits::of(x) & MathBits::magnitudeMask()) | (MathBits::of(y) & MathBits::signBit()));
}
