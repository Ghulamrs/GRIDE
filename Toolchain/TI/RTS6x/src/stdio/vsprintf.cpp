// Spec: ISO C 7.19.6.13 - vsprintf, sprintf with a va_list.

#include <stdio.h>
#include <stdarg.h>
#include "Formatter.h"

extern "C" int vsprintf(char *s, const char *format, va_list args)
{
    rts6x::OutputSink out(s, (size_t)-1);
    return rts6x::Formatter(out, &args).run(rts6x::FormatText(format));
}
