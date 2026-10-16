// Spec: ISO C 7.12.4.2 - asin.

#include <math.h>
#include "ArcTangent.h"

extern "C" double asin(double x)
{
    return rts6x::ArcTangent::arcSine(x);
}
