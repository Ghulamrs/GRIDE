// Spec: ISO C99 7.12.9.7 lround.
#include <math.h>
#include "IntegralPart.h"
extern "C" long lround(double x)
{
    return rts6x::IntegralPart::toLong(rts6x::IntegralPart::nearestAway(x));
}
