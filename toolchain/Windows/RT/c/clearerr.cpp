// Spec: ISO C 7.19.10.1 - clearerr clears both indicators. RTS6x's, with the stream locked for the call
// (StreamWin.h): the indicators share a word with the flags the other calls change.
#include "StreamWin.h"

extern "C" void clearerr(FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    rts6x::Stream(stream).clear();
    grt::unlock(stream, held);
}
