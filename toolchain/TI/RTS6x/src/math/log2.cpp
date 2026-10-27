// Spec: ISO C99 7.12.6.10 - log2.
#include <math.h>
#include "Logarithm.h"
extern "C" double log2(double x)
{
    return rts6x::Logarithm::binary(x);
}
