// Spec: Microsoft C runtime, _vsnprintf - at most n characters into s, the NUL only where there is room for it:
// the count when the whole text fit (n itself when it filled s exactly, unterminated), -1 when it did not.

#include <stdio.h>
#include <stdarg.h>
#include "Formatter.h"

extern "C" int _vsnprintf(char *s, size_t n, const char *format, va_list args)
{
    rts6x::OutputSink out(s, n, true);
    int count = rts6x::Formatter(out, &args).run(rts6x::FormatText(format));
    return count >= 0 && static_cast<size_t>(count) <= n ? count : -1;
}