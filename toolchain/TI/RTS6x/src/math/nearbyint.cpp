// Spec: ISO C99 7.12.9.3 nearbyint - rint's value, raising no inexact: nothing here touches the floating point. On
// Windows it is c\rint.asm (rts6x-c.sources).
#include <math.h>
#include "IntegralPart.h"
extern "C" double nearbyint(double x)
{
    return rts6x::IntegralPart::nearestEven(x);
}
