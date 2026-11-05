// uchar.h - [c.strings] Table 75: conversions between multibyte characters and char16_t and char32_t,
// which are keywords in C++. The UCRT and glibc have them; Darwin's C library does not.
#ifndef _CXX1_UCHAR_H
#define _CXX1_UCHAR_H

#include <wchar.h>

#if defined(__APPLE__) || defined(__TMS320C6X__)
#error "<uchar.h>: this target's C library has no char16_t or char32_t conversions"
#endif

#ifdef __cplusplus
extern "C" {
#else
// C11's: in C the two are this header's typedefs, uint_least16_t and uint_least32_t.
typedef unsigned short char16_t;
typedef unsigned int char32_t;
#endif

size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
