// Spec: ISO C 7.19.6.8 - vfprintf, fprintf with a va_list. <stdarg.h> declares the stream as void *, so it is
// defined so here; it is the FILE the caller passed. RTS6x's, with the stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "stdio/Formatter.h"
#include "StreamWin.h"

extern "C" int vfprintf(void *file, const char *format, va_list args)
{
    FILE *stream = static_cast<FILE *>(file);
    grt::valid(stream);
    bool held = grt::lock(stream);
    rts6x::Stream to(stream);
    rts6x::OutputSink out(to.writeDescriptor());
    int n = to.wrote(rts6x::Formatter(out, &args).run(rts6x::FormatText(format)));
    grt::unlock(stream, held);
    return n;
}
