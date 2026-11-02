// Spec: ISO C 7.12 - exp, log, sin, cos and pow where c\mathfast.asm does not take the case itself: RTS6x's kernels,
// which give every special value and the errno the Microsoft runtime sets (a NaN, an infinity, a zero, a subnormal,
// a range error, a very large argument of sin and cos, pow with x not positive). RTS6x's own exp.cpp ... pow.cpp, one
// line each over the same kernels, are left out of grtc (rts6x-c.sources).
#include "math/Exponential.h"
#include "math/Logarithm.h"
#include "math/Power.h"
#include "math/Trigonometric.h"

extern "C" double __grt_exp_slow(double x)
{
    return rts6x::Exponential::natural(x);
}

extern "C" double __grt_log_slow(double x)
{
    return rts6x::Logarithm::natural(x);
}

extern "C" double __grt_sin_slow(double x)
{
    return rts6x::Trigonometric::sine(x);
}

extern "C" double __grt_cos_slow(double x)
{
    return rts6x::Trigonometric::cosine(x);
}

extern "C" double __grt_pow_slow(double x, double y)
{
    return rts6x::Power::raise(x, y);
}
