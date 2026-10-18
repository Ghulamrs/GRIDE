// Spec: ISO C 7.21.2.1 - memcpy, the C++ reference of memcpy.s: a word at a time where both ends
// are word-aligned, else a byte. Built only with RTS6X_REFERENCE defined (make reference), which
// leaves memcpy.s out of that library; otherwise this file compiles to nothing.
#ifdef RTS6X_REFERENCE
#include <string.h>

extern "C" void *memcpy(void *to, const void *from, size_t n)
{
    unsigned char *d = (unsigned char *)to;
    const unsigned char *s = (const unsigned char *)from;
    if ((((size_t)d | (size_t)s) & 3) == 0)
        for (; n >= 4; n -= 4, d += 4, s += 4) *(unsigned *)d = *(const unsigned *)s;
    while (n--) *d++ = *s++;
    return to;
}

#endif
