// Spec: ISO C 7.19.6.8 - vfprintf, fprintf with a va_list. <stdarg.h> declares the stream as void *,
// so it is defined so here; it is the FILE the caller passed.

#include <stdio.h>
#include <stdarg.h>
#include "Formatter.h"
#include "Stream.h"

extern "C" int vfprintf(void *stream, const char *format, va_list args)
{
    rts6x::Stream to(static_cast<FILE *>(stream));
    rts6x::OutputSink out(to.writeDescriptor());
    return to.wrote(rts6x::Formatter(out, &args).run(rts6x::FormatText(format)));
}
