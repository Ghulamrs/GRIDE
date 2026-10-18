// Spec: ISO C 7.21.4.2 - strcmp, the C++ reference of strcmp.s: the first differing character,
// as unsigned char, decides. Built only with RTS6X_REFERENCE defined (make reference), which
// leaves strcmp.s out of that library; otherwise this file compiles to nothing.
#ifdef RTS6X_REFERENCE
#include <string.h>

extern "C" int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

#endif
