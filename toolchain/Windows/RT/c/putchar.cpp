// Spec: ISO C 7.19.7.9 - putchar is putc to stdout.
#include "StreamWin.h"

extern "C" int putchar(int c)
{
    FILE *stream = &_ftable[1];
    if (!__grt_threads && (stream->flags & grt::Pending) && stream->pos < stream->buff_stop &&
        (c != '\n' || !(stream->flags & grt::TextOut))) {
        *stream->pos++ = (unsigned char)c;
        return (unsigned char)c;
    }
    return grt::putLocked(c, stream);
}
