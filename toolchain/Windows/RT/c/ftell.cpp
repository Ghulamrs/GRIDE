// Spec: ISO C 7.19.9.4 - ftell: the position, or -1L. RTS6x's, with the stream locked for the call (StreamWin.h).
#include "StreamWin.h"

extern "C" long ftell(FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    long result = rts6x::Stream(stream).tell();
    grt::unlock(stream, held);
    return result;
}
