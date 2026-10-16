// Spec: ISO C 7.12.9.1 - ceil.

#include <math.h>
#include "IntegralPart.h"

extern "C" double ceil(double x)
{
    return rts6x::IntegralPart::up(x);
}
