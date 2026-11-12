// Spec: Microsoft C runtime, _strnicmp - _stricmp over at most n bytes; 0 when n is 0.

#include <string.h>

extern "C" int _strnicmp(const char *a, const char *b, size_t n)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(a);
    const unsigned char *q = reinterpret_cast<const unsigned char *>(b);
    int f = 0, l = 0;
    while (n-- != 0) {
        f = *p++;
        l = *q++;
        if (f >= 'A' && f <= 'Z') f += 'a' - 'A';
        if (l >= 'A' && l <= 'Z') l += 'a' - 'A';
        if (f == 0 || f != l) break;
    }
    return f - l;
}