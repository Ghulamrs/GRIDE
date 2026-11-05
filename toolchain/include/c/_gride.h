// _gride.h - what the C headers of c90 and cpp11 share (docs\Runtime_Design.md 5.2): one set for both compilers,
// C90 first and the later C library behind _GRIDE_C99_LIBRARY.
#ifndef _GRIDE_H
#define _GRIDE_H

// C99's and C11's additions: cpp11 sees them, as C++11 includes them; c90 sees C90 unless a project defines
// _GRIDE_C99.
#if defined(__cplusplus) || defined(__STDC_VERSION__) || defined(_GRIDE_C99)
#define _GRIDE_C99_LIBRARY 1
#endif

// The functions a header defines. c90 has no inline, so there they are static functions; in C++ the two keep
// the linkage each header gave them.
#if defined(__cplusplus)
#define _GRIDE_INLINE inline
#define _GRIDE_STATIC_INLINE static inline
#elif defined(__STDC_VERSION__)
#define _GRIDE_INLINE static inline
#define _GRIDE_STATIC_INLINE static inline
#else
#define _GRIDE_INLINE static
#define _GRIDE_STATIC_INLINE static
#endif

#endif
