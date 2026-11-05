// Spec: ISO C99 7.20.6.2 - lldiv.

#include <stdlib.h>

extern "C" lldiv_t lldiv(long long numerator, long long denominator)
{
    lldiv_t r;
    r.quot = numerator / denominator;
    r.rem = numerator % denominator;
    return r;
}
