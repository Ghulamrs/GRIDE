// Spec: ISO C 7.19.6.8 - vfprintf, fprintf with a va_list. <stdarg.h> declares the stream as void *, so it is
// defined so here; it is the FILE the caller passed. The format as the Microsoft runtime reads it
// (fmt\Printer.h), with the stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "fmt/Printer.h"
#include "StreamWin.h"

extern "C" int vfprintf(void *file, const char *format, va_list args)
{
    FILE *stream = static_cast<FILE *>(file);
    grt::valid(stream);
    bool held = grt::lock(stream);
    int n = grt::printToStream(stream, format, &args);
    grt::unlock(stream, held);
    return n;
}
