// Spec: ISO C 7.19.6.4 - scanf is fscanf on stdin.

#include <stdio.h>
#include <stdarg.h>
#include "Scanner.h"

extern "C" int scanf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    rts6x::InputSource in(stdin);
    int n = rts6x::Scanner(in, &args).run(format);
    va_end(args);
    return n;
}
