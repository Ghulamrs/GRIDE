// Spec: ISO C 7.12.6.4 - frexp.

#include <math.h>
#include "BinaryScale.h"

extern "C" double frexp(double x, int *exponent)
{
    return rts6x::BinaryScale::fraction(x, exponent);
}
