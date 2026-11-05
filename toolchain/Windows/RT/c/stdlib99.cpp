// Spec: ISO C99 7.20 - llabs. atoll is RTS6x's; MB_CUR_MAX and the multibyte functions are c\multibyte.cpp.
#include <stdlib.h>

extern "C" long long llabs(long long v)
{
    return v < 0 ? -v : v;
}
