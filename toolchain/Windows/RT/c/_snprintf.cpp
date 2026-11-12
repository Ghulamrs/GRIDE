// Spec: Microsoft C runtime, _snprintf - at most n characters into s, the NUL only where there is room for it: the
// count when the whole text fit (n itself when it filled s exactly, unterminated), -1 when it did not. The format as
// snprintf reads it (fmt\Printer.h); a null s with n above 0 fails fast, as the UCRT's invalid parameter does.
#include <stdarg.h>
#include "fmt/Printer.h"

extern "C" int _snprintf(char *s, size_t n, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (!s && n != 0) __grt_fail_fast();
    int count = grt::printToMemoryWhole(s, n, format, &args);
    va_end(args);
    return count;
}