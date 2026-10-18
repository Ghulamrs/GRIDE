// Spec: ISO C 7.12.4.7 - tan.

#include <math.h>
#include "Trigonometric.h"

extern "C" double tan(double x)
{
    return rts6x::Trigonometric::tangent(x);
}
