// Spec: SPRAB89B 8.2 Table 8-12 - __c6xabi_trunc, the integer toward zero, ISO C99 7.12.9.8.

#include "SoftFloat.h"

extern "C" double __c6xabi_trunc(double x)
{
    return rts6x::FloatBits::toDouble(rts6x::FloatArithmetic::truncate(rts6x::FloatBits::of(x)));
}
