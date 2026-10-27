// Spec: ISO C 7.19.7.4 - fputs writes the string, without its NUL; non-negative, or EOF on error. RTS6x's, with
// the stream locked for the call (StreamWin.h).
#include "stdio/OutputSink.h"
#include "StreamWin.h"

extern "C" int fputs(const char *s, FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    rts6x::Stream to(stream);
    rts6x::OutputSink out(to.writeDescriptor());
    out.put(s);
    int n = to.wrote(out.finish());
    grt::unlock(stream, held);
    return n < 0 ? EOF : 0;
}
