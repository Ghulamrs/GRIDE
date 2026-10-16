// Spec: ISO C 7.19.6.2 - fscanf reads the stream under control of the format.

#include <stdio.h>
#include <stdarg.h>
#include "Scanner.h"

extern "C" int fscanf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    rts6x::InputSource in(stream);
    int n = rts6x::Scanner(in, &args).run(format);
    va_end(args);
    return n;
}
