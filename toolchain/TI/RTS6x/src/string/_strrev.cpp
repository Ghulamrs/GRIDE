// Spec: Microsoft C runtime, _strrev - the bytes of s before its NUL in the opposite order, in place; s.

#include <string.h>

extern "C" char *_strrev(char *s)
{
    char *low = s, *high = s + strlen(s);
    while (low + 1 < high) {
        char c = *low;
        *low++ = *--high;
        *high = c;
    }
    return s;
}