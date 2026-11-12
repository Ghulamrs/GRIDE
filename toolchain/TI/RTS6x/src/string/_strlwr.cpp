// Spec: Microsoft C runtime, _strlwr - ASCII's capitals in s lowered in place, as the "C" locale lowers them; s.

#include <string.h>

extern "C" char *_strlwr(char *s)
{
    for (char *p = s; *p != 0; p++)
        if (*p >= 'A' && *p <= 'Z') *p = static_cast<char>(*p + ('a' - 'A'));
    return s;
}