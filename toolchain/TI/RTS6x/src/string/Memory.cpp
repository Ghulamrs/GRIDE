// Spec: ISO C 7.21.4.1 and 7.21.5.1 - memcmp (the first differing byte, as unsigned char, decides)
// and memchr. memcmp steps over equal aligned words before it looks at bytes.

#include "Memory.h"

namespace rts6x {

int Memory::compare(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    if (aligned(x, y))
        for (; n >= 4 && *(const unsigned *)x == *(const unsigned *)y; n -= 4) { x += 4; y += 4; }
    for (; n; n--, x++, y++)
        if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}

void *Memory::find(const void *in, int value, size_t n)
{
    const unsigned char *p = (const unsigned char *)in;
    unsigned char b = (unsigned char)value;
    for (; n; n--, p++)
        if (*p == b) return (void *)p;
    return 0;
}

}  // namespace rts6x
