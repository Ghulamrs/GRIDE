// Spec: ISO C99 7.12.6.13 scalbn - ldexp's rounding and errno.
#include <math.h>
#include "BinaryScale.h"
extern "C" double scalbn(double x, int n)
{
    return rts6x::BinaryScale::scale(x, n);
}
