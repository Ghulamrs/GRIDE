// Spec: ISO C 7.20.6.1 - abs.

#include <stdlib.h>

extern "C" int abs(int n)
{
    return n < 0 ? -n : n;
}
