// Spec: SPRAB89B 8.2 Table 8-2 - __c6xabi_fltulld, uint64 to binary64.

#include "SoftFloat.h"

extern "C" double __c6xabi_fltulld(unsigned long long x)
{
    return rts6x::FloatBits::toDouble(rts6x::FloatArithmetic::fromInteger(rts6x::FloatFormat::binary64(), x, false));
}
