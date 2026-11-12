// Spec: Microsoft C runtime, _snprintf - _vsnprintf with its arguments here.

#include <stdio.h>
#include <stdarg.h>

extern "C" int _snprintf(char *s, size_t n, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int count = _vsnprintf(s, n, format, args);
    va_end(args);
    return count;
}