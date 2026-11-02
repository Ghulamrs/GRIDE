// Spec: ISO C 7.19.6.6 - sprintf writes into the array s and ends it with a NUL, not counted. The format as the
// Microsoft runtime reads it (fmt\Printer.h); a null s fails fast, and a failed call (-1) still ends what it wrote
// with a NUL, as the runtime's does.
#include <stdarg.h>
#include "fmt/Printer.h"

extern "C" int sprintf(char *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (!s) __grt_fail_fast();
    int n = grt::printToMemory(s, (size_t)-1, false, format, &args);
    va_end(args);
    return n;
}
