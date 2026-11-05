// Spec: ISO C99 7.12.9.5 llrint.
#include <math.h>
#include "IntegralPart.h"
extern "C" long long llrint(double x)
{
    return rts6x::IntegralPart::toLongLong(rint(x));
}
