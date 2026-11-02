// Spec: ISO C 7.19.6.3 - printf is fprintf to stdout. The format as the Microsoft runtime reads it
// (fmt\Printer.h), with the stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "fmt/Printer.h"
#include "StreamWin.h"

extern "C" int printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    FILE *stream = &_ftable[1];
    bool held = grt::lock(stream);
    int n = grt::printToStream(stream, format, &args);
    grt::unlock(stream, held);
    va_end(args);
    return n;
}
