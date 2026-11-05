// Spec: ISO C99 7.12.13.1 fma - the Microsoft runtime's answer where the processor has no fused multiply-add
// (c\fma.asm): RTS6x's exact sum. RTS6x's own fma.cpp, one line over it, is left out of grtc (rts6x-c.sources).
#include "math/FusedMultiply.h"

extern "C" double __grt_fma_soft(double x, double y, double z)
{
    return rts6x::FusedMultiply::evaluate(x, y, z);
}
