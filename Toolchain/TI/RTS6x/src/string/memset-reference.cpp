// Spec: ISO C 7.21.6.1 - memset, the C++ reference of memset.s: a word of four copies of the byte
// where the destination is word-aligned, else a byte. Built only with RTS6X_REFERENCE defined
// (make reference), which leaves memset.s out of that library; otherwise this compiles to nothing.
#ifdef RTS6X_REFERENCE
#include <string.h>

extern "C" void *memset(void *to, int value, size_t n)
{
    unsigned char *d = (unsigned char *)to;
    unsigned char b = (unsigned char)value;
    if (((size_t)d & 3) == 0) {
        unsigned word = b * 0x01010101u;
        for (; n >= 4; n -= 4, d += 4) *(unsigned *)d = word;
    }
    while (n--) *d++ = b;
    return to;
}

#endif
