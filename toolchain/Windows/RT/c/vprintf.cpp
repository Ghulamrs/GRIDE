// Spec: ISO C 7.19.6.10 - vprintf is printf with the arguments already in a va_list. The format as the
// Microsoft runtime reads it (fmt\Printer.h), with the stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "fmt/Printer.h"
#include "StreamWin.h"

extern "C" int vprintf(const char *format, va_list args)
{
    FILE *stream = &_ftable[1];
    bool held = grt::lock(stream);
    int n = grt::printToStream(stream, format, &args);
    grt::unlock(stream, held);
    return n;
}
