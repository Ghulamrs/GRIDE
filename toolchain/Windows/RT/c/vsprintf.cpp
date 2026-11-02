// Spec: ISO C 7.19.6.13 - vsprintf, sprintf with a va_list; as sprintf.cpp says.
#include <stdarg.h>
#include "fmt/Printer.h"

extern "C" int vsprintf(char *s, const char *format, va_list args)
{
    if (!s) __grt_fail_fast();
    return grt::printToMemory(s, (size_t)-1, false, format, &args);
}
