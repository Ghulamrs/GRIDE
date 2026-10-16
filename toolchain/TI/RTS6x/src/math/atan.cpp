// Spec: ISO C 7.12.4.3 - atan.

#include <math.h>
#include "ArcTangent.h"

extern "C" double atan(double x)
{
    return rts6x::ArcTangent::arcTangent(x);
}
