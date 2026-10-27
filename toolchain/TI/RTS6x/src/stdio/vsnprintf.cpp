// Spec: ISO C99 7.19.6.12 - vsnprintf, snprintf with a va_list: at most n - 1 characters and a NUL (nothing
// when n is 0), and the count the whole output would have had.
#include <stdio.h>
#include <stdarg.h>
#include "Formatter.h"
extern "C" int vsnprintf(char *s, size_t n, const char *format, va_list args)
{
    rts6x::OutputSink out(s, n);
    return rts6x::Formatter(out, &args).run(rts6x::FormatText(format));
}
