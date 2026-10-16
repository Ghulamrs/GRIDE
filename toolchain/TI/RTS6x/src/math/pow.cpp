// Spec: ISO C 7.12.7.4 - pow.

#include <math.h>
#include "Power.h"

extern "C" double pow(double x, double y)
{
    return rts6x::Power::raise(x, y);
}
