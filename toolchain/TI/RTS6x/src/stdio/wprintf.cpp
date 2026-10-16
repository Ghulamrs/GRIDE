// Spec: ISO C 7.24.2.11 - wprintf is fwprintf to stdout: a wide format, each wide character written
// as its multibyte form - in the "C" locale, one byte for one up to 0xFF, an encoding error beyond.
#include <stdarg.h>
#include "Formatter.h"
#include "Stream.h"

extern "C" int wprintf(const wchar_t *format, ...)
{
    va_list args;
    va_start(args, format);
    rts6x::Stream to(stdout);
    rts6x::OutputSink out(to.writeDescriptor());
    int n = rts6x::Formatter(out, &args).run(rts6x::FormatText(format));
    va_end(args);
    return to.wrote(n);
}
