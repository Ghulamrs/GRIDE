// stdlib.h - allocation, conversion, and leaving.
//
// The same bargain as <stdio.h>: prototypes only, checked against glibc by the
// suite building every case twice. See that file for why this is not a copy of
// the real header.
//
// malloc needed nothing new from the compiler when it first appeared here - it
// arrives through an ordinary prototype, and the cast that turns its block into
// an array of something is the declarator grammar doing its ordinary work.
#ifndef _CC1_STDLIB_H
#define _CC1_STDLIB_H

#include <_gride.h>

// Everything here is the C library, so it is named the way C names it.
// Without this the compiler would give every declaration below a C++
// linkage name and ask the linker for a symbol libc has never had.
#ifdef __cplusplus
extern "C" {
#endif


#include <stddef.h>

// C90 7.10: the two exit statuses everywhere, and RAND_MAX as each platform's own library has it -
// glibc's and Darwin's 2147483647, Microsoft's and the C6000's (RTS6x's, and TI's ABI) 32767.
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#if defined(_WIN32) || defined(__TMS320C6X__)
#define RAND_MAX 32767
#else
#define RAND_MAX 2147483647
#endif

// C99 7.20/3: MB_CUR_MAX, the most bytes a character takes in the current locale - each library's own function
// (the UCRT's, glibc's, Darwin's); the C6000's library has single-byte characters only.
#if defined(_WIN32)
int ___mb_cur_max_func(void);
#define MB_CUR_MAX ((size_t)___mb_cur_max_func())
#elif defined(__APPLE__)
int ___mb_cur_max(void);
#define MB_CUR_MAX ((size_t)___mb_cur_max())
#elif defined(__TMS320C6X__)
#define MB_CUR_MAX ((size_t)1)
#else
size_t __ctype_get_mb_cur_max(void);
#define MB_CUR_MAX (__ctype_get_mb_cur_max())
#endif

void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);

void exit(int);
void abort(void);
// C99's and C++11's other ways out: a handler run at exit, the quick pair that
// runs only its own handlers ([support.start.term]), and one that runs none.
int atexit(void (*)(void));
#ifdef _GRIDE_C99_LIBRARY
int at_quick_exit(void (*)(void));
void quick_exit(int);
void _Exit(int);
#endif
// [cstdlib.syn]: a command handed to the host's command processor; its status comes back.
int system(const char *);

int atoi(const char *);
char *getenv(const char *);
long atol(const char *);
#ifdef _GRIDE_C99_LIBRARY
long long atoll(const char *);
#endif

// The one that says where it stopped. atol cannot report a failure at all - it
// gives back zero for "0" and for "banana" alike - so anything reading a
// number it did not write itself wants this instead, and gets the rest of the
// string as well as the value.
//
// char ** rather than const char **, which looks like an oversight and is not:
// it is what C89 says, so that the end pointer can be used to walk a string
// the caller owns. Declaring it the tidier way would make every correct
// program that passes a char ** fail to compile here.
//
// strtol and strtoul are its neighbours and take a base as well: 0 reads the
// prefix - 0x for sixteen, a leading 0 for eight - and 2 to 36 says outright.
double atof(const char *);
double strtod(const char *, char **);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
// C99's, which C++11's [c.strings] takes: the 64-bit and the narrower and wider floating forms.
#ifdef _GRIDE_C99_LIBRARY
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
float strtof(const char *, char **);
long double strtold(const char *, char **);
#endif

int abs(int);
long labs(long);
#ifdef _GRIDE_C99_LIBRARY
long long llabs(long long);
#endif

// The program's arguments, for an entry that is not given them (WinMain), as the UCRT's header spells them: a
// program with a narrow entry has __argv, one with a wide entry __wargv, and the other is a null pointer.
#if defined(_WIN32)
int *__p___argc(void);
char ***__p___argv(void);
wchar_t ***__p___wargv(void);
#define __argc (*__p___argc())
#define __argv (*__p___argv())
#define __wargv (*__p___wargv())
#endif

// The quotient and the remainder of one division, returned as a structure. The
// tags are the UCRT's; glibc's and Darwin's structures have none.
#if defined(_WIN32)
typedef struct _div_t { int quot; int rem; } div_t;
typedef struct _ldiv_t { long quot; long rem; } ldiv_t;
#ifdef _GRIDE_C99_LIBRARY
typedef struct _lldiv_t { long long quot; long long rem; } lldiv_t;
#endif
#else
typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
#ifdef _GRIDE_C99_LIBRARY
typedef struct { long long quot; long long rem; } lldiv_t;
#endif
#endif
#ifndef __cplusplus
div_t div(int, int);
#endif
ldiv_t ldiv(long, long);
#ifdef _GRIDE_C99_LIBRARY
lldiv_t lldiv(long long, long long);
#endif

int rand(void);
void srand(unsigned int);

// The multibyte conversions, in the current locale - which a program that never
// calls setlocale leaves "C", where a character is one byte.
int mblen(const char *, size_t);
int mbtowc(wchar_t *, const char *, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
size_t wcstombs(char *, const wchar_t *, size_t);

// The two that could not be written here until pointers to functions existed,
// and a good part of the reason they were wanted. A comparison function is the
// argument, and there is no way to spell one without this type.
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t,
              int (*)(const void *, const void *));

#ifdef __cplusplus
double fabs(double);
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__TMS320C6X__)
long double fabsl(long double);
#endif
#endif


#ifdef __cplusplus
}  // extern "C"

// **`abs` and `div` are C++ overload sets here, not C's two functions** - one
// per argument type, each answering in that type, as [c.math]/7-9 and cl's own
// headers give them. C's `abs(int)` above keeps its C linkage, which a C
// runtime defining it needs, and the C++ overloads sit beside it. `div(int, int)`
// is a C++ function among them. <cstdlib> names the whole set in `std` with
// one using-declaration, and <cmath> reaches it through <cstdlib>.
inline long abs(long v) { return labs(v); }
inline long long abs(long long v) { return llabs(v); }
inline float abs(float v) { return (float)fabs(v); }
inline double abs(double v) { return fabs(v); }
#if defined(_WIN32) || defined(__APPLE__) || defined(__TMS320C6X__)
inline long double abs(long double v) { return fabs((double)v); }
#else
inline long double abs(long double v) { return fabsl(v); }
#endif

inline div_t div(int n, int d) { div_t r; r.quot = n / d; r.rem = n % d; return r; }
inline ldiv_t div(long n, long d) { return ldiv(n, d); }
inline lldiv_t div(long long n, long long d) { return lldiv(n, d); }
#endif

#endif
