// Spec: ISO C 7.19.6.3 - printf is fprintf to stdout, whose descriptor is 1.
#include <stdio.h>
#include <stdarg.h>
#include "Formatter.h"
#include "Stream.h"

extern "C" int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    rts6x::Stream to(stdout);
    rts6x::OutputSink out(to.writeDescriptor());
    int n = rts6x::Formatter(out, &args).run(rts6x::FormatText(format));
    va_end(args);
    return to.wrote(n);
}
