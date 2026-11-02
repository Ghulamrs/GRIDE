// Spec: ISO C99 7.4.1.3 - isblank, in the "C" locale a space or a tab, answered as the Microsoft runtime's table
// answers (_BLANK, 0x40); ISO C 7.21.3.2 - strncat; 7.21.4.3 and 7.21.4.5 - strcoll and strxfrm, which in the "C"
// locale compare and copy as strcmp and strcpy do.
#include <ctype.h>
#include <string.h>

extern "C" int isblank(int c)
{
    return c == ' ' || c == '\t' ? 0x40 : 0;
}

extern "C" char *strncat(char *to, const char *from, size_t n)
{
    char *end = to + strlen(to);
    while (n-- && *from) *end++ = *from++;
    *end = 0;
    return to;
}

extern "C" int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

extern "C" size_t strxfrm(char *to, const char *from, size_t n)
{
    size_t length = strlen(from);
    if (to && length < n) memcpy(to, from, length + 1);
    return length;
}
