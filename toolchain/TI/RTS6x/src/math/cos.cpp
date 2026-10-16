// Spec: ISO C 7.12.4.5 - cos.

#include <math.h>
#include "Trigonometric.h"

extern "C" double cos(double x)
{
    return rts6x::Trigonometric::cosine(x);
}
