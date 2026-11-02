// Spec: ISO C99 7.19.6.9, 7.19.6.11 and 7.19.6.12 - vfscanf, vscanf and vsscanf: fscanf, scanf and sscanf with the
// arguments in a va_list. RTS6x's Scanner, the stream locked for the call as fscanf locks it (StreamWin.h). The
// compilers' <stdarg.h> declares vfscanf's stream as a void *, which needs no FILE there.
#include <stdarg.h>
#include "stdio/Scanner.h"
#include "StreamWin.h"

extern "C" int vfscanf(void *file, const char *format, va_list args)
{
    FILE *stream = static_cast<FILE *>(file);
    grt::valid(stream);
    va_list mine;
    va_copy(mine, args);
    bool held = grt::lock(stream);
    rts6x::InputSource in(stream);
    int n = rts6x::Scanner(in, &mine).run(format);
    grt::unlock(stream, held);
    va_end(mine);
    return n;
}

extern "C" int vscanf(const char *format, va_list args)
{
    return vfscanf(&_ftable[0], format, args);
}

extern "C" int vsscanf(const char *s, const char *format, va_list args)
{
    va_list mine;
    va_copy(mine, args);
    rts6x::InputSource in(s);
    int n = rts6x::Scanner(in, &mine).run(format);
    va_end(mine);
    return n;
}
