// Spec: ISO C99 7.20.1.4 - strtoll.
#include <stdlib.h>
#include "NumberText.h"
extern "C" long long strtoll(const char *s, char **end, int base)
{
    return (long long)rts6x::NumberText::convertLong((const unsigned char *)s, end, base, 1);
}
