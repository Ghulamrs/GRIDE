// Spec: ISO C 7.19.6.3 - printf is fprintf to stdout. RTS6x's, with the stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "stdio/Formatter.h"
#include "StreamWin.h"

extern "C" int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    FILE *stream = &_ftable[1];
    bool held = grt::lock(stream);
    rts6x::Stream to(stream);
    rts6x::OutputSink out(to.writeDescriptor());
    int n = to.wrote(rts6x::Formatter(out, &args).run(rts6x::FormatText(format)));
    grt::unlock(stream, held);
    va_end(args);
    return n;
}
