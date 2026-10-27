// Spec: ISO C 7.19.6.4 - scanf is fscanf on stdin. RTS6x's, with the stream locked for the call (StreamWin.h).
#include <stdarg.h>
#include "stdio/Scanner.h"
#include "StreamWin.h"

extern "C" int scanf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    FILE *stream = &_ftable[0];
    bool held = grt::lock(stream);
    rts6x::InputSource in(stream);
    int n = rts6x::Scanner(in, &args).run(format);
    grt::unlock(stream, held);
    va_end(args);
    return n;
}
