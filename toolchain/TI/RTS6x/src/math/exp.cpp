// Spec: ISO C 7.12.6.1 - exp.

#include <math.h>
#include "Exponential.h"

extern "C" double exp(double x)
{
    return rts6x::Exponential::natural(x);
}
