// Spec: ISO C90 7.9.7.7 - gets reads stdin up to a new-line, which it drops, and stores a NUL; s, or
// a null pointer at the end of the file with nothing read, or on a read error.

#include "Stream.h"

extern "C" char *gets(char *s)
{
    rts6x::Stream in(stdin);
    int i = 0;
    for (;;) {
        int c = in.get();
        if (c == EOF) {
            if (i == 0 || in.failed()) return 0;
            break;
        }
        if (c == '\n') break;
        s[i++] = (char)c;
    }
    s[i] = 0;
    return s;
}
