// Spec: ISO C 7.19.7.2 - fgets reads up to n-1 bytes, through a new-line, and stores a NUL; s, or a null pointer
// at the end of the file with nothing read, or on a read error. RTS6x's, with the stream locked for the call
// (StreamWin.h).
#include "StreamWin.h"

extern "C" char *fgets(char *s, int n, FILE *stream)
{
    if (n <= 0) return 0;
    grt::valid(stream);
    bool held = grt::lock(stream);
    rts6x::Stream in(stream);
    char *result = s;
    int i = 0;
    while (i < n - 1) {
        int c = in.get();
        if (c == EOF) {
            if (i == 0 || in.failed()) result = 0;
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (result) s[i] = 0;
    grt::unlock(stream, held);
    return result;
}
