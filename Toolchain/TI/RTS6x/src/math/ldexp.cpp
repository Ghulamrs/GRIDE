// Spec: ISO C 7.12.6.6 - ldexp.

#include <math.h>
#include "BinaryScale.h"

extern "C" double ldexp(double x, int n)
{
    return rts6x::BinaryScale::scale(x, n);
}
