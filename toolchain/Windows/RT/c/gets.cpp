// Spec: ISO C90 7.9.7.7 - gets reads stdin up to a new-line, which it drops, and stores a NUL; s, or a null
// pointer at the end of the file with nothing read, or on a read error. RTS6x's, with the stream locked for the
// call (StreamWin.h).
#include "StreamWin.h"

extern "C" char *gets(char *s)
{
    FILE *stream = &_ftable[0];
    bool held = grt::lock(stream);
    rts6x::Stream in(stream);
    char *result = s;
    int i = 0;
    for (;;) {
        int c = in.get();
        if (c == EOF) {
            if (i == 0 || in.failed()) result = 0;
            break;
        }
        if (c == '\n') break;
        s[i++] = (char)c;
    }
    if (result) s[i] = 0;
    grt::unlock(stream, held);
    return result;
}
