// Spec: ISO C99 7.12.10.3 remquo.
#include <math.h>
#include "Remainder.h"
extern "C" double remquo(double x, double y, int *quotient)
{
    return rts6x::Remainder::nearest(x, y, quotient);
}
