// Spec: SPRAB89B 8.2 Table 8-2 - __c6xabi_fltullf, uint64 to binary32.

#include "SoftFloat.h"

extern "C" float __c6xabi_fltullf(unsigned long long x)
{
    return rts6x::FloatBits::toFloat(rts6x::FloatArithmetic::fromInteger(rts6x::FloatFormat::binary32(), x, false));
}
