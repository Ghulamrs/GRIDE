// Spec: ISO C99 7.20.1.4 - strtoull.
#include <stdlib.h>
#include "NumberText.h"
extern "C" unsigned long long strtoull(const char *s, char **end, int base)
{
    return rts6x::NumberText::convertLong((const unsigned char *)s, end, base, 0);
}
