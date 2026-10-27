// Spec: ISO C 7.19.7.5 - getc is fgetc.
#include "StreamWin.h"

extern "C" int getc(FILE *stream)
{
    if (!stream) __grt_fail_fast();
    if (!__grt_threads && stream->pos < stream->bufend) {
        unsigned char c = *stream->pos;
        if (!(stream->flags & grt::TextIn) || (c != '\r' && c != grt::CtrlZ)) {
            stream->pos++;
            return c;
        }
    }
    return grt::getLocked(stream);
}
