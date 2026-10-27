// Spec: ISO C 7.19.7.11 - ungetc pushes c back to be read next; c, or EOF if it cannot. RTS6x's, with the
// stream locked for the call (StreamWin.h).
#include "StreamWin.h"

extern "C" int ungetc(int c, FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    int result = rts6x::Stream(stream).unget(c);
    grt::unlock(stream, held);
    return result;
}
