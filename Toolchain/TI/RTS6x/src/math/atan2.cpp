// Spec: ISO C 7.12.4.4 - atan2.

#include <math.h>
#include "ArcTangent.h"

extern "C" double atan2(double y, double x)
{
    return rts6x::ArcTangent::arcTangent2(y, x);
}
