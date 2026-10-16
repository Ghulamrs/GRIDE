// Spec: ISO C 7.20.6.1 - labs.

#include <stdlib.h>

extern "C" long labs(long n)
{
    return n < 0 ? -n : n;
}
