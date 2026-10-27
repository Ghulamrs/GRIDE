// Spec: ISO C 7.19.7.3 - fputc writes c as an unsigned char; c so written, or EOF on error. A buffered stream
// in writing takes it straight into its buffer while the program has one thread; anything else (an LF in text
// mode, a full buffer, a console, a program with threads) goes through the stream layer, under the stream's lock.
#include "StreamWin.h"

extern "C" int fputc(int c, FILE *stream)
{
    if (!stream) __grt_fail_fast();
    if (!__grt_threads && (stream->flags & grt::Pending) && stream->pos < stream->buff_stop &&
        (c != '\n' || !(stream->flags & grt::TextOut))) {
        *stream->pos++ = (unsigned char)c;
        return (unsigned char)c;
    }
    return grt::putLocked(c, stream);
}
