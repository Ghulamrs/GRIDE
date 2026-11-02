// Spec: ISO C 7.19.6.1 - fprintf writes to the stream. The format as the Microsoft runtime reads it
// (fmt\Printer.h), with the stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "fmt/Printer.h"
#include "StreamWin.h"

extern "C" int fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    grt::valid(stream);
    bool held = grt::lock(stream);
    int n = grt::printToStream(stream, format, &args);
    grt::unlock(stream, held);
    va_end(args);
    return n;
}
