// Spec: ISO C 7.12.4.6 - sin.

#include <math.h>
#include "Trigonometric.h"

extern "C" double sin(double x)
{
    return rts6x::Trigonometric::sine(x);
}
