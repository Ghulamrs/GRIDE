// fenv.h - [cfenv]: the floating-point environment, its exception flags and rounding modes.
//
// The types and numbers are each C library's own: the UCRT's (read from its fenv.h and float.h), glibc's
// on x86-64 and Darwin's on arm64. The UCRT defines feraiseexcept and feupdateenv in its header and exports
// neither, so here they are inline as they are there: an exception is raised by the operation that raises
// it, through volatile operands no fold can remove. The C6747 has no floating-point environment to reach.
#ifndef _CXX1_FENV_H
#define _CXX1_FENV_H

#if defined(__TMS320C6X__)
#error "<fenv.h>: the C6747 runtime has no floating-point environment"
#endif

#include <_gride.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
typedef unsigned long fexcept_t;
typedef struct fenv_t { unsigned long _Fe_ctl, _Fe_stat; } fenv_t;
#define FE_INEXACT    0x00000001
#define FE_UNDERFLOW  0x00000002
#define FE_OVERFLOW   0x00000004
#define FE_DIVBYZERO  0x00000008
#define FE_INVALID    0x00000010
#define FE_TONEAREST  0x00000000
#define FE_DOWNWARD   0x00000100
#define FE_UPWARD     0x00000200
#define FE_TOWARDZERO 0x00000300
static const fenv_t __cxx1_fe_dfl_env = { 0x3f00003f, 0 };
#define FE_DFL_ENV (&__cxx1_fe_dfl_env)
#elif defined(__APPLE__)
typedef unsigned short fexcept_t;
typedef struct { unsigned long long __fpsr; unsigned long long __fpcr; } fenv_t;
#define FE_INVALID    0x0001
#define FE_DIVBYZERO  0x0002
#define FE_OVERFLOW   0x0004
#define FE_UNDERFLOW  0x0008
#define FE_INEXACT    0x0010
#define FE_TONEAREST  0x00000000
#define FE_UPWARD     0x00400000
#define FE_DOWNWARD   0x00800000
#define FE_TOWARDZERO 0x00C00000
extern const fenv_t _FE_DFL_ENV;
#define FE_DFL_ENV (&_FE_DFL_ENV)
#else
typedef unsigned short fexcept_t;
typedef struct {
    unsigned short __control_word, __reserved1, __status_word, __reserved2, __tags, __reserved3;
    unsigned int __eip;
    unsigned short __cs_selector, __opcode;
    unsigned int __data_offset;
    unsigned short __data_selector, __reserved5;
    unsigned int __mxcsr;
} fenv_t;
#define FE_INVALID    0x01
#define FE_DIVBYZERO  0x04
#define FE_OVERFLOW   0x08
#define FE_UNDERFLOW  0x10
#define FE_INEXACT    0x20
#define FE_TONEAREST  0x0000
#define FE_DOWNWARD   0x0400
#define FE_UPWARD     0x0800
#define FE_TOWARDZERO 0x0c00
#define FE_DFL_ENV ((const fenv_t *)-1)
#endif

#define FE_ALL_EXCEPT (FE_DIVBYZERO | FE_INEXACT | FE_INVALID | FE_OVERFLOW | FE_UNDERFLOW)

int feclearexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int fesetexceptflag(const fexcept_t *flagp, int excepts);
int fetestexcept(int excepts);
int fegetround(void);
int fesetround(int round);
int fegetenv(fenv_t *envp);
int feholdexcept(fenv_t *envp);
int fesetenv(const fenv_t *envp);

#if defined(_WIN32)
_GRIDE_INLINE int feraiseexcept(int excepts) {
    static const struct { int except; double num, denom; } table[] = {
        { FE_INVALID, 0.0, 0.0 }, { FE_DIVBYZERO, 1.0, 0.0 }, { FE_OVERFLOW, 1e+300, 1e-300 },
        { FE_UNDERFLOW, 1e-300, 1e+300 }, { FE_INEXACT, 2.0, 3.0 }
    };
    unsigned i;
    if ((excepts & FE_ALL_EXCEPT) != excepts) return 1;
    for (i = 0; i < sizeof table / sizeof table[0]; i++)
        if ((excepts & table[i].except) != 0) {
            volatile double num = table[i].num, denom = table[i].denom, sink;
            sink = num / denom;
            (void)sink;
        }
    return 0;
}
_GRIDE_INLINE int feupdateenv(const fenv_t *envp) {
    const int raised = fetestexcept(FE_ALL_EXCEPT);
    if (fesetenv(envp) != 0) return 1;
    return feraiseexcept(raised);
}
#else
int feraiseexcept(int excepts);
int feupdateenv(const fenv_t *envp);
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
