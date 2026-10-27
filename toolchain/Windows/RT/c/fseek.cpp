// Spec: ISO C 7.19.9.2 - fseek: 0, or nonzero for a request that cannot be satisfied. RTS6x's, with the stream
// locked for the call (StreamWin.h).
#include "StreamWin.h"

extern "C" int fseek(FILE *stream, long offset, int whence)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    int result = rts6x::Stream(stream).seek(offset, whence);
    grt::unlock(stream, held);
    return result;
}
