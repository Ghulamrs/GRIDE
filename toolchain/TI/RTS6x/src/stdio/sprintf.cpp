// Spec: ISO C 7.19.6.6 - sprintf writes into the array s and ends it with a NUL, not counted.
#include <stdio.h>
#include <stdarg.h>
#include "Formatter.h"

extern "C" int sprintf(char *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    rts6x::OutputSink out(s, (size_t)-1);
    int n = rts6x::Formatter(out, &args).run(rts6x::FormatText(format));
    va_end(args);
    return n;
}
