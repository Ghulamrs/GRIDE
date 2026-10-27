// Spec: ISO C 7.19.7.6 - getchar is getc from stdin.
#include "StreamWin.h"

extern "C" int getchar(void)
{
    FILE *stream = &_ftable[0];
    if (!__grt_threads && stream->pos < stream->bufend) {
        unsigned char c = *stream->pos;
        if (!(stream->flags & grt::TextIn) || (c != '\r' && c != grt::CtrlZ)) {
            stream->pos++;
            return c;
        }
    }
    return grt::getLocked(stream);
}
