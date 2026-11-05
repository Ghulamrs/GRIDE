// Spec: ISO C 7.21.3.2 - strncat; 7.21.4.5 - strxfrm, which in the "C" locale copies as strcpy does. isblank and strcoll
// are RTS6x's.
#include <string.h>

extern "C" char *strncat(char *to, const char *from, size_t n)
{
    char *end = to + strlen(to);
    while (n-- && *from) *end++ = *from++;
    *end = 0;
    return to;
}

extern "C" size_t strxfrm(char *to, const char *from, size_t n)
{
    size_t length = strlen(from);
    if (to && length < n) memcpy(to, from, length + 1);
    return length;
}
