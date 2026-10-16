// Spec: ISO C 7.12.10.1 - fmod.

#include <math.h>
#include "Remainder.h"

extern "C" double fmod(double x, double y)
{
    return rts6x::Remainder::truncated(x, y);
}
