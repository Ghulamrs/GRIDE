// Spec: ISO C99 7.19.6.5 - snprintf writes at most n - 1 characters and a NUL (nothing when n is 0) and answers
// the count the whole output would have had. The format as the Microsoft runtime reads it (fmt\Printer.h); a null
// s with n above 0 fails fast, and a failed call (-1) leaves s empty, as the runtime's does.
#include <stdarg.h>
#include "fmt/Printer.h"

extern "C" int snprintf(char *s, size_t n, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    if (!s && n != 0) __grt_fail_fast();
    int count = grt::printToMemory(s, n, true, format, &args);
    va_end(args);
    return count;
}
