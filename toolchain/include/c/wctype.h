// wctype.h - [c.strings] Table 73: wide character classes and mappings. wctype_t and wctrans_t are each C
// library's: the UCRT's are both unsigned short-sized (wctype_t its own, wctrans_t wchar_t), glibc's
// unsigned long and a pointer, Darwin's unsigned long and int.
#ifndef _CXX1_WCTYPE_H
#define _CXX1_WCTYPE_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
typedef unsigned short wctype_t;
typedef wchar_t wctrans_t;
#elif defined(__APPLE__)
typedef unsigned long wctype_t;
typedef int wctrans_t;
#else
typedef unsigned long wctype_t;
typedef const int *wctrans_t;
#endif

int iswalnum(wint_t wc);
int iswalpha(wint_t wc);
int iswblank(wint_t wc);
int iswcntrl(wint_t wc);
int iswdigit(wint_t wc);
int iswgraph(wint_t wc);
int iswlower(wint_t wc);
int iswprint(wint_t wc);
int iswpunct(wint_t wc);
int iswspace(wint_t wc);
int iswupper(wint_t wc);
int iswxdigit(wint_t wc);
int iswctype(wint_t wc, wctype_t desc);
wctype_t wctype(const char *property);
wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);
wint_t towctrans(wint_t wc, wctrans_t desc);
wctrans_t wctrans(const char *property);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
