// Spec: ISO C 7.19.6.7 - sscanf reads the string, whose end is the end of the input.

#include <stdio.h>
#include <stdarg.h>
#include "Scanner.h"

extern "C" int sscanf(const char *s, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    rts6x::InputSource in(s);
    int n = rts6x::Scanner(in, &args).run(format);
    va_end(args);
    return n;
}
