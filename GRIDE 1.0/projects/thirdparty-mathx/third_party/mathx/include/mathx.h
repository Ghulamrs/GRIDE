/* mathx.h - the whole public interface of the mathx library, 1.0.
 *
 * A third-party library as one usually arrives: this header, and a prebuilt
 * binary for each platform under ../libs. It is plain C with C linkage, so a
 * C program and a C++ program can both call it, whichever compiler built it. */
#ifndef MATHX_H
#define MATHX_H

#ifdef __cplusplus
extern "C" {
#endif

const char *mathx_version(void);
int         mathx_gcd(int a, int b);
long long   mathx_factorial(int n);
double      mathx_mean(const double *values, int count);
double      mathx_sqrt(double x);            /* Newton's method, to 1e-12 */

#ifdef __cplusplus
}
#endif

#endif
