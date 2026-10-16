// Spec: ISO C 7.19.7.4 - fputs writes the string, without its NUL; non-negative, or EOF on error.

#include <stdio.h>
#include "OutputSink.h"
#include "Stream.h"

extern "C" int fputs(const char *s, FILE *stream)
{
    rts6x::Stream to(stream);
    rts6x::OutputSink out(to.writeDescriptor());
    out.put(s);
    return to.wrote(out.finish()) < 0 ? EOF : 0;
}
