// Spec: ISO C99 7.12.9.7 llround.
#include <math.h>
#include "IntegralPart.h"
extern "C" long long llround(double x)
{
    return rts6x::IntegralPart::toLongLong(rts6x::IntegralPart::nearestAway(x));
}
