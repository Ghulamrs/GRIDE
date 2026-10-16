// Spec: ISO C 7.12.6.8 - log10.

#include <math.h>
#include "Logarithm.h"

extern "C" double log10(double x)
{
    return rts6x::Logarithm::common(x);
}
