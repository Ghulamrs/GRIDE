// Spec: ISO C 7.12.9.2 - floor.

#include <math.h>
#include "IntegralPart.h"

extern "C" double floor(double x)
{
    return rts6x::IntegralPart::down(x);
}
