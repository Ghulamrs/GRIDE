// Spec: ISO C99 7.19.6.5 - snprintf writes at most n - 1 characters and a NUL (nothing when n is 0)
// and returns the count the whole output would have had.

#include <stdio.h>
#include <stdarg.h>
#include "Formatter.h"

extern "C" int snprintf(char *s, size_t n, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    rts6x::OutputSink out(s, n);
    int count = rts6x::Formatter(out, &args).run(rts6x::FormatText(format));
    va_end(args);
    return count;
}
