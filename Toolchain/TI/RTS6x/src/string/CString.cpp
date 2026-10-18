// Spec: ISO C 7.21.2.3-4, 7.21.3.1, 7.21.4.2 and 7.21.4.4, 7.21.5.2-8: strcpy strncpy (padding with
// NULs), strcat, strcmp strncmp (as unsigned char), strchr strrchr (the NUL itself findable),
// strstr (an empty text found at once), strpbrk strspn strcspn, and strtok.

#include "CString.h"
#include "Memory.h"

namespace rts6x {

char *CString::next_;

char *CString::copy(char *to, const char *from)
{
    return (char *)Memory::copy(to, from, length(from) + 1);
}

char *CString::copy(char *to, const char *from, size_t n)
{
    size_t i = 0;
    for (; i < n && from[i]; i++) to[i] = from[i];
    for (; i < n; i++) to[i] = 0;
    return to;
}

char *CString::append(char *to, const char *from)
{
    copy(to + length(to), from);
    return to;
}

int CString::compare(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
    }
    return 0;
}

char *CString::findLast(const char *s, int c)
{
    const char *last = 0;
    for (const char *p = find(s, c); p; p = *p ? find(p + 1, c) : 0) last = p;
    return (char *)last;
}

char *CString::find(const char *s, const char *text)
{
    size_t n = length(text);
    for (; *s; s++)
        if (compare(s, text, n) == 0) return (char *)s;
    return n == 0 ? (char *)s : 0;
}

char *CString::findAny(const char *s, const char *set)
{
    for (; *s; s++)
        if (in(*s, set)) return (char *)s;
    return 0;
}

size_t CString::span(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n] && in(s[n], set)) n++;
    return n;
}

size_t CString::spanNot(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n] && !in(s[n], set)) n++;
    return n;
}

char *CString::token(char *s, const char *separators)
{
    if (s == 0) s = next_;
    if (s == 0) return 0;
    s += span(s, separators);
    if (!*s) { next_ = 0; return 0; }
    char *end = s + spanNot(s, separators);
    if (*end) { *end = 0; next_ = end + 1; }
    else next_ = 0;
    return s;
}

}  // namespace rts6x
