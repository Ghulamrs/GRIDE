// Spec: ISO C 7.12.7.2 - fabs.

#include <math.h>
#include "BinaryScale.h"

extern "C" double fabs(double x)
{
    return rts6x::BinaryScale::magnitude(x);
}
