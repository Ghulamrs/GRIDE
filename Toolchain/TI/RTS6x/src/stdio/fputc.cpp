// Spec: ISO C 7.19.7.3 - fputc writes c as an unsigned char; c so written, or EOF on error.

#include <stdio.h>
#include "OutputSink.h"
#include "Stream.h"

extern "C" int fputc(int c, FILE *stream)
{
    rts6x::Stream to(stream);
    rts6x::OutputSink out(to.writeDescriptor());
    out.put((char)(unsigned char)c);
    return to.wrote(out.finish()) < 0 ? EOF : (unsigned char)c;
}
