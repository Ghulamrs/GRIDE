// Spec: ISO C 7.20.1.4 - strtoul.

#include <stdlib.h>
#include "NumberText.h"

extern "C" unsigned long strtoul(const char *s, char **end, int base)
{
    return rts6x::NumberText::convert((const unsigned char *)s, end, base, 0);
}
