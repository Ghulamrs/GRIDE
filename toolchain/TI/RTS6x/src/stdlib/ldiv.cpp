// Spec: ISO C 7.20.6.2 - ldiv.

#include <stdlib.h>

extern "C" ldiv_t ldiv(long numerator, long denominator)
{
    ldiv_t r;
    r.quot = numerator / denominator;
    r.rem = numerator % denominator;
    return r;
}
