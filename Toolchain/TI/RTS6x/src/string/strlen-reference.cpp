// Spec: ISO C 7.21.6.3 - strlen, the C++ reference of strlen.s: a byte at a time to the NUL.
// Built only with RTS6X_REFERENCE defined (make reference), which leaves strlen.s out of that
// library; otherwise this file compiles to nothing.
#ifdef RTS6X_REFERENCE
#include <string.h>

extern "C" size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

#endif
