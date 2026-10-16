// Spec: SPRAB89B 8.2 Table 8-1 - __c6xabi_fixdlli, binary64 to int64.

#include "SoftFloat.h"

extern "C" long long __c6xabi_fixdlli(double x)
{
    return rts6x::FloatArithmetic::toSigned64(rts6x::FloatFormat::binary64(), rts6x::FloatBits::of(x));
}
