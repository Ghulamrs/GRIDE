// Spec: ISO C 7.21.2.2 - memmove, the C++ reference of memmove.s: memcpy where the destination does
// not start inside the source, else bytes from the end backwards. Built only with RTS6X_REFERENCE
// defined (make reference), which leaves memmove.s out of that library; otherwise nothing.
#ifdef RTS6X_REFERENCE
#include <string.h>

extern "C" void *memmove(void *to, const void *from, size_t n)
{
    unsigned char *d = (unsigned char *)to;
    const unsigned char *s = (const unsigned char *)from;
    if (d <= s || d >= s + n) return memcpy(to, from, n);
    while (n--) d[n] = s[n];
    return to;
}

#endif
