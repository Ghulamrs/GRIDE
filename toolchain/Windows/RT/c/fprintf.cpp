// Spec: ISO C 7.19.6.1 - fprintf writes to the stream. RTS6x's, with the stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "stdio/Formatter.h"
#include "StreamWin.h"

extern "C" int fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    grt::valid(stream);
    bool held = grt::lock(stream);
    rts6x::Stream to(stream);
    rts6x::OutputSink out(to.writeDescriptor());
    int n = to.wrote(rts6x::Formatter(out, &args).run(rts6x::FormatText(format)));
    grt::unlock(stream, held);
    va_end(args);
    return n;
}
