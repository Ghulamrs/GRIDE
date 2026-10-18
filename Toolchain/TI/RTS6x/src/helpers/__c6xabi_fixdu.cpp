// Spec: SPRAB89B 8.2 Table 8-1 - __c6xabi_fixdu, binary64 to uint32.

#include "SoftFloat.h"

extern "C" unsigned __c6xabi_fixdu(double x)
{
    return (unsigned)rts6x::FloatArithmetic::toUnsigned(rts6x::FloatFormat::binary64(), rts6x::FloatBits::of(x), 32);
}
