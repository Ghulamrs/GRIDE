// Spec: SPRAB89B 8.2 Table 8-1 - __c6xabi_fixdull, binary64 to uint64.

#include "SoftFloat.h"

extern "C" unsigned long long __c6xabi_fixdull(double x)
{
    return rts6x::FloatArithmetic::toUnsigned(rts6x::FloatFormat::binary64(), rts6x::FloatBits::of(x), 64);
}
