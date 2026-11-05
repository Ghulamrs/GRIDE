// Spec: ISO C99 7.12.13.1 fma. On Windows it is c\fma.asm, the processor's instruction where it has one, over the same
// exact sum (rts6x-c.sources).
#include <math.h>
#include "FusedMultiply.h"
extern "C" double fma(double x, double y, double z)
{
    return rts6x::FusedMultiply::evaluate(x, y, z);
}
