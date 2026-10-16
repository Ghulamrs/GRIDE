// Spec: ISO C 7.12.5.6 - tanh.

#include <math.h>
#include "Hyperbolic.h"

extern "C" double tanh(double x)
{
    return rts6x::Hyperbolic::tangent(x);
}
