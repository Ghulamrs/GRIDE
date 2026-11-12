// Spec: Microsoft C runtime, _strdup - a copy of s in storage from malloc, which the caller frees; a null pointer
// when s is null, or when malloc has none (errno ENOMEM, malloc's).

#include <stdlib.h>
#include <string.h>

extern "C" char *_strdup(const char *s)
{
    if (s == 0) return 0;
    size_t n = strlen(s) + 1;
    char *copy = static_cast<char *>(malloc(n));
    if (copy != 0) memcpy(copy, s, n);
    return copy;
}