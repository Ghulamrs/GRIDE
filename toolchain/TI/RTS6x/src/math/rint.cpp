// Spec: ISO C99 7.12.9.4 rint - to an integer in the rounding direction, which on the C6747 is to nearest, ties to even.
// RTS6x keeps no other; on Windows rint is c\rint.asm, which follows the processor's (rts6x-c.sources).
#include <math.h>
#include "IntegralPart.h"
extern "C" double rint(double x)
{
    return rts6x::IntegralPart::nearestEven(x);
}
