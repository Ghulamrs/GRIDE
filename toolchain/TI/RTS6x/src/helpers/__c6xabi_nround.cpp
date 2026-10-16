// Spec: SPRAB89B 8.2 Table 8-12 - __c6xabi_nround, the nearest integer, halves away from zero, ISO C99 7.12.9.6.

#include "SoftFloat.h"

extern "C" double __c6xabi_nround(double x)
{
    return rts6x::FloatBits::toDouble(rts6x::FloatArithmetic::roundHalfAway(rts6x::FloatBits::of(x)));
}
