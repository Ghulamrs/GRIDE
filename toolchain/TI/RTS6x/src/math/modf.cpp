// Spec: ISO C 7.12.6.12 - modf.

#include <math.h>
#include "IntegralPart.h"

extern "C" double modf(double x, double *whole)
{
    return rts6x::IntegralPart::split(x, whole);
}
