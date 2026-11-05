// Spec: ISO C99 7.12.9.5 lrint.
#include <math.h>
#include "IntegralPart.h"
extern "C" long lrint(double x)
{
    return rts6x::IntegralPart::toLong(rint(x));
}
