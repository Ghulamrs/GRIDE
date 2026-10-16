// Spec: ISO C 7.21.5.2 - strchr, the C++ reference of strchr.s: the first c, the NUL itself
// findable. Built only with RTS6X_REFERENCE defined (make reference), which leaves strchr.s out
// of that library; otherwise this file compiles to nothing.
#ifdef RTS6X_REFERENCE
#include <string.h>

extern "C" char *strchr(const char *s, int c)
{
    for (char ch = (char)c;; s++) {
        if (*s == ch) return (char *)s;
        if (!*s) return 0;
    }
}

#endif
