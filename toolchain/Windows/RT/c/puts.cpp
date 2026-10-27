// Spec: ISO C 7.19.7.10 - puts writes the string and a new-line to stdout; non-negative, or EOF. RTS6x's, with
// the stream locked for the call (StreamWin.h).
#include "stdio/OutputSink.h"
#include "StreamWin.h"

extern "C" int puts(const char *s)
{
    FILE *stream = &_ftable[1];
    bool held = grt::lock(stream);
    rts6x::Stream to(stream);
    rts6x::OutputSink out(to.writeDescriptor());
    out.put(s);
    out.put('\n');
    int n = to.wrote(out.finish());
    grt::unlock(stream, held);
    return n < 0 ? EOF : 0;
}
