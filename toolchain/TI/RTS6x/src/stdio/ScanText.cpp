// Spec: ISO C 7.19.6.2/12 - c (exactly the width's characters, 1 by default, no NUL), s (a run of
// non-white-space, then a NUL) and [ (a run of the scanset's characters, then a NUL; ^ inverts, a
// ] first is a member). With l the characters are stored wide.

#include "Scanner.h"
#include "../ctype/CharacterClass.h"

namespace rts6x {

void Scanner::store(void *target, int i, int c, Length length)
{
    if (length == Long) static_cast<wchar_t *>(target)[i] = (wchar_t)c;
    else static_cast<char *>(target)[i] = (char)c;
}

bool Scanner::characters(bool assign, int width, Length length, bool &input)
{
    void *target = assign ? va_arg(*args_, void *) : 0;
    int count = width > 0 ? width : 1;
    for (int i = 0; i < count; i++) {
        int c = in_.get();
        if (c == EOF) {
            input = i == 0;
            if (i == 0) return false;
            break;
        }
        if (target) store(target, i, c, length);
    }
    if (target) assigned_++;
    return true;
}

bool Scanner::word(bool assign, int width, Length length, bool &input)
{
    void *target = assign ? va_arg(*args_, void *) : 0;
    int left = width > 0 ? width : 0x7FFFFFFF, n = 0;
    int c = in_.get();
    for (; c >= 0 && !CharacterClass::space(c); c = advance(left)) {
        if (target) store(target, n, c, length);
        n++;
    }
    giveBack(c);
    if (n == 0) {
        input = c == EOF;
        return false;
    }
    if (target) {
        store(target, n, 0, length);
        assigned_++;
    }
    return true;
}

bool Scanner::scanset(bool assign, int width, Length length, const char *&format, bool &input)
{
    bool invert = *format == '^';
    if (invert) format++;
    const char *set = format;
    if (*format == ']') format++;
    while (*format && *format != ']') format++;
    const char *setEnd = format;
    if (*format == ']') format++;
    void *target = assign ? va_arg(*args_, void *) : 0;
    int left = width > 0 ? width : 0x7FFFFFFF, n = 0;
    int c = in_.get();
    for (; c >= 0; c = advance(left)) {
        bool member = false;
        for (const char *p = set; p < setEnd && !member; p++) {
            if (p[1] == '-' && p + 2 < setEnd) {
                member = c >= (unsigned char)p[0] && c <= (unsigned char)p[2];
                p += 2;
            } else {
                member = c == (unsigned char)*p;
            }
        }
        if (member == invert) break;
        if (target) store(target, n, c, length);
        n++;
    }
    giveBack(c);
    if (n == 0) {
        input = c == EOF;
        return false;
    }
    if (target) {
        store(target, n, 0, length);
        assigned_++;
    }
    return true;
}

}  // namespace rts6x
