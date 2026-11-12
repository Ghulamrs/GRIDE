// Spec: Microsoft C runtime, _stricmp - a and b compared with ASCII's capitals lowered, as the "C" locale lowers
// them: the difference of the first two bytes that differ so, as the UCRT answers (`_stricmp("Z", "a")` is 25).

#include <string.h>

extern "C" int _stricmp(const char *a, const char *b)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(a);
    const unsigned char *q = reinterpret_cast<const unsigned char *>(b);
    int f, l;
    do {
        f = *p++;
        l = *q++;
        if (f >= 'A' && f <= 'Z') f += 'a' - 'A';
        if (l >= 'A' && l <= 'Z') l += 'a' - 'A';
    } while (f != 0 && f == l);
    return f - l;
}