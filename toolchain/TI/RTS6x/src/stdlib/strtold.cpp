// Spec: ISO C99 7.20.1.3 - strtold. A long double is a double on both of RTS6x's targets (the C6000 EABI and
// Microsoft's x64), so it is strtod.
#include <stdlib.h>
extern "C" long double strtold(const char *s, char **end)
{
    return strtod(s, end);
}
