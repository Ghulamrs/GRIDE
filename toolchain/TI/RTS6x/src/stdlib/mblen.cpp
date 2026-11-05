// Spec: ISO C 7.20.7.1 - mblen in the "C" locale, as the Microsoft runtime's: a character is one byte and no encoding
// has a shift state.
#include <stdlib.h>
extern "C" int mblen(const char *s, size_t n)
{
    if (!s || n == 0 || !*s) return 0;
    return 1;
}
