// Spec: ISO C 7.24.2.11 - wprintf is fwprintf to stdout: a wide format, each wide character written as its
// multibyte form - in the "C" locale, one byte for one up to 0xFF, an encoding error beyond. RTS6x's, with the
// stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "stdio/Formatter.h"
#include "StreamWin.h"

extern "C" int wprintf(const wchar_t *format, ...)
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
