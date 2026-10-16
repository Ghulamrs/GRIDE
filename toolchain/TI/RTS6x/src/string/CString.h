// Spec: ISO C 7.21.2-7.21.5 - the str* functions over NUL-terminated strings, characters compared as
// unsigned char; strtok's place between calls kept by the class, the one state this family has.
#ifndef RTS6X_CSTRING_H
#define RTS6X_CSTRING_H

#include <stddef.h>

extern "C" size_t strlen(const char *s);       // strlen.s
extern "C" char *strchr(const char *s, int c);  // strchr.s
extern "C" int strcmp(const char *a, const char *b);   // strcmp.s

namespace rts6x {

class CString {
public:
    static size_t length(const char *s) { return strlen(s); }
    static char *copy(char *to, const char *from);
    static char *copy(char *to, const char *from, size_t n);
    static char *append(char *to, const char *from);
    static int compare(const char *a, const char *b) { return strcmp(a, b); }
    static int compare(const char *a, const char *b, size_t n);
    static char *find(const char *s, int c) { return strchr(s, c); }
    static char *findLast(const char *s, int c);
    static char *find(const char *s, const char *text);
    static char *findAny(const char *s, const char *set);
    static size_t span(const char *s, const char *set);
    static size_t spanNot(const char *s, const char *set);
    static char *token(char *s, const char *separators);

private:
    static bool in(char c, const char *set) { for (; *set; set++) if (*set == c) return true; return false; }
    static char *next_;
};

}  // namespace rts6x

#endif
