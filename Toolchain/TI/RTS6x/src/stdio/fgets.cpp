// Spec: ISO C 7.19.7.2 - fgets reads up to n-1 bytes, through a new-line, and stores a NUL; s, or a
// null pointer at the end of the file with nothing read, or on a read error.

#include "Stream.h"

extern "C" char *fgets(char *s, int n, FILE *stream)
{
    rts6x::Stream in(stream);
    int i = 0;
    while (i < n - 1) {
        int c = in.get();
        if (c == EOF) {
            if (i == 0 || in.failed()) return 0;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (n > 0) s[i] = 0;
    return n > 0 ? s : 0;
}
