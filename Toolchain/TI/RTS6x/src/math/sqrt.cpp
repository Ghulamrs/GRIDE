// Spec: ISO C 7.12.7.5 - sqrt.

#include <math.h>
#include "SquareRoot.h"

extern "C" double sqrt(double x)
{
    return rts6x::SquareRoot::root(x);
}
