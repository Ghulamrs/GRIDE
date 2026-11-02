// Spec: ISO C99 7.19.6.12 - vsnprintf, snprintf with a va_list; as snprintf.cpp says.
#include <stdarg.h>
#include "fmt/Printer.h"

extern "C" int vsnprintf(char *s, size_t n, const char *format, va_list args)
{
    if (!s && n != 0) __grt_fail_fast();
    return grt::printToMemory(s, n, true, format, &args);
}
