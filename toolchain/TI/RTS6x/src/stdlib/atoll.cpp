// Spec: ISO C99 7.20.1.2 - atoll: strtoll in base 10 with no end pointer.
#include <stdlib.h>
extern "C" long long atoll(const char *s)
{
    return strtoll(s, 0, 10);
}
