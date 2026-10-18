// Spec: ISO C 7.12.5.5 - sinh.

#include <math.h>
#include "Hyperbolic.h"

extern "C" double sinh(double x)
{
    return rts6x::Hyperbolic::sine(x);
}
