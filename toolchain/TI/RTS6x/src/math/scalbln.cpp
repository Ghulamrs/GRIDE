// Spec: ISO C99 7.12.6.13 scalbln - ldexp's rounding and errno; a long is as wide as an int on both targets.
#include <math.h>
#include "BinaryScale.h"
extern "C" double scalbln(double x, long n)
{
    return rts6x::BinaryScale::scale(x, (int)n);
}
