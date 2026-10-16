// Spec: ISO C 7.19.6.1 - fprintf writes to the stream, here to its host descriptor.
#include <stdio.h>
#include <stdarg.h>
#include "Formatter.h"
#include "Stream.h"

extern "C" int fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    rts6x::Stream to(stream);
    rts6x::OutputSink out(to.writeDescriptor());
    int n = rts6x::Formatter(out, &args).run(rts6x::FormatText(format));
    va_end(args);
    return to.wrote(n);
}
