// Spec: ISO C 7.12.5.4 - cosh.

#include <math.h>
#include "Hyperbolic.h"

extern "C" double cosh(double x)
{
    return rts6x::Hyperbolic::cosine(x);
}
