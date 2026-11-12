// Spec: Microsoft C runtime's old name itoa for _itoa (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <stdlib.h>

extern "C" char *itoa(int value, char *buf, int radix)
{
    return _itoa(value, buf, radix);
}
