/* itoa.c - the old name Microsoft keeps in oldnames.lib for _itoa (c90), in grtcompat. */
#define _POSIX_SOURCE 1
#include <stdlib.h>

char *itoa(int value, char *buf, int radix)
{
    return _itoa(value, buf, radix);
}
