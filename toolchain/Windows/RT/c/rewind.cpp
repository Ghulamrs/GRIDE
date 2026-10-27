// Spec: ISO C 7.19.9.5 - rewind is fseek to the start with the error indicator cleared as well. RTS6x's, with
// the stream locked for the call (StreamWin.h).
#include "StreamWin.h"

extern "C" void rewind(FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    rts6x::Stream s(stream);
    s.seek(0, SEEK_SET);
    s.clear();
    grt::unlock(stream, held);
}
