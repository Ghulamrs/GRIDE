// Spec: Microsoft C runtime, _vsnprintf - _snprintf with a va_list; as _snprintf.cpp says.
#include <stdarg.h>
#include "fmt/Printer.h"

extern "C" int _vsnprintf(char *s, size_t n, const char *format, va_list args)
{
    if (!s && n != 0) __grt_fail_fast();
    return grt::printToMemoryWhole(s, n, format, &args);
}