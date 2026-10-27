// Spec: ISO C 7.19.7.8 - putc is fputc.
#include "StreamWin.h"

extern "C" int putc(int c, FILE *stream)
{
    if (!stream) __grt_fail_fast();
    if (!__grt_threads && (stream->flags & grt::Pending) && stream->pos < stream->buff_stop &&
        (c != '\n' || !(stream->flags & grt::TextOut))) {
        *stream->pos++ = (unsigned char)c;
        return (unsigned char)c;
    }
    return grt::putLocked(c, stream);
}
