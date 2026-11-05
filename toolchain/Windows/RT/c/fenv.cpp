// Spec: ISO C 7.6 (fenv.h) as the Microsoft runtime has it on x64 (tests\wide\wide.expected), found by probing it:
// - Only SSE's state counts, MXCSR's: the x87 unit's flags, masks and rounding are neither read nor set.
// - The runtime's abstract bits: flags and masks inexact 1, underflow 2, overflow 4, zero-divide 8, invalid 0x10,
//   denormal 0x20; rounding 0 to nearest, 0x100 down, 0x200 up, 0x300 toward zero.
// - fexcept_t and fenv_t's status hold the flags twice, (s << 24) | s; fenv_t's control holds the masks and the
//   rounding the same way, the high copy's rounding shifted right by 2. FE_DFL_ENV is { 0x3f00003f, 0 }.
// - fesetround of another value answers 1 and changes nothing; feholdexcept keeps the environment, then clears the
//   flags and masks every exception. feraiseexcept and feupdateenv are the header's own.
#include <fenv.h>

extern "C" unsigned __grt_mxcsr(void);
extern "C" void __grt_set_mxcsr(unsigned value);

namespace {

// MXCSR's flag bits (0-5) and mask bits (7-12) for the abstract bits 1, 2, 4, 8, 0x10 and 0x20
const unsigned flagBits[6] = { 0x20, 0x10, 0x08, 0x04, 0x01, 0x02 };
const unsigned maskBits[6] = { 0x1000, 0x800, 0x400, 0x200, 0x80, 0x100 };

unsigned flags(unsigned mxcsr)
{
    unsigned a = 0;
    for (int i = 0; i < 6; i++) if (mxcsr & flagBits[i]) a |= 1u << i;
    return a;
}

unsigned masks(unsigned mxcsr)
{
    unsigned a = 0;
    for (int i = 0; i < 6; i++) if (mxcsr & maskBits[i]) a |= 1u << i;
    return a;
}

unsigned withFlags(unsigned mxcsr, unsigned a)
{
    for (int i = 0; i < 6; i++) mxcsr = (a & (1u << i)) ? mxcsr | flagBits[i] : mxcsr & ~flagBits[i];
    return mxcsr;
}

unsigned withMasks(unsigned mxcsr, unsigned a)
{
    for (int i = 0; i < 6; i++) mxcsr = (a & (1u << i)) ? mxcsr | maskBits[i] : mxcsr & ~maskBits[i];
    return mxcsr;
}

unsigned rounding(unsigned mxcsr) { return ((mxcsr >> 13) & 3) << 8; }
unsigned withRounding(unsigned mxcsr, unsigned r) { return (mxcsr & ~0x6000u) | (((r >> 8) & 3) << 13); }
unsigned twice(unsigned a) { return (a << 24) | a; }

}  // namespace

extern "C" int feclearexcept(int excepts)
{
    if ((excepts & FE_ALL_EXCEPT) != excepts) return 1;
    unsigned m = __grt_mxcsr();
    __grt_set_mxcsr(withFlags(m, flags(m) & ~(unsigned)excepts));
    return 0;
}

extern "C" int fetestexcept(int excepts)
{
    return (int)(flags(__grt_mxcsr()) & (unsigned)excepts & FE_ALL_EXCEPT);
}

extern "C" int fegetexceptflag(fexcept_t *saved, int excepts)
{
    *saved = twice(flags(__grt_mxcsr()) & (unsigned)excepts & FE_ALL_EXCEPT);
    return 0;
}

extern "C" int fesetexceptflag(const fexcept_t *saved, int excepts)
{
    unsigned e = (unsigned)excepts & FE_ALL_EXCEPT;
    unsigned wanted = (unsigned)(*saved | (*saved >> 24)) & e;
    unsigned m = __grt_mxcsr();
    __grt_set_mxcsr(withFlags(m, (flags(m) & ~e) | wanted));
    return 0;
}

extern "C" int fegetround(void)
{
    return (int)rounding(__grt_mxcsr());
}

extern "C" int fesetround(int round)
{
    if (round != FE_TONEAREST && round != FE_DOWNWARD && round != FE_UPWARD && round != FE_TOWARDZERO) return 1;
    __grt_set_mxcsr(withRounding(__grt_mxcsr(), (unsigned)round));
    return 0;
}

extern "C" int fegetenv(fenv_t *env)
{
    unsigned m = __grt_mxcsr();
    unsigned c = masks(m), r = rounding(m);
    env->_Fe_ctl = ((c | (r >> 2)) << 24) | c | r;
    env->_Fe_stat = twice(flags(m));
    return 0;
}

extern "C" int fesetenv(const fenv_t *env)
{
    unsigned m = __grt_mxcsr();
    m = withMasks(m, env->_Fe_ctl & 0x3F);
    m = withRounding(m, env->_Fe_ctl & 0x300);
    m = withFlags(m, env->_Fe_stat & 0x3F);
    __grt_set_mxcsr(m);
    return 0;
}

extern "C" int feholdexcept(fenv_t *env)
{
    fegetenv(env);
    unsigned m = __grt_mxcsr();
    __grt_set_mxcsr(withMasks(withFlags(m, 0), 0x3F));
    return 0;
}
