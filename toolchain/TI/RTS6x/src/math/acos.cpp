// Spec: ISO C 7.12.4.1 - acos.

#include <math.h>
#include "ArcTangent.h"

extern "C" double acos(double x)
{
    return rts6x::ArcTangent::arcCosine(x);
}
