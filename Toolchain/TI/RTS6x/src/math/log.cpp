// Spec: ISO C 7.12.6.7 - log.

#include <math.h>
#include "Logarithm.h"

extern "C" double log(double x)
{
    return rts6x::Logarithm::natural(x);
}
