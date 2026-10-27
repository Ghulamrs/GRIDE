// Spec: ISO C 7.19.7.1 - fgetc: the next character as an unsigned char, or EOF. A byte in the buffer that
// needs no translation is given at once while the program has one thread; a CR or CTRL+Z of a text stream, an
// empty buffer or a program with threads goes through the stream layer, under the stream's lock.
#include "StreamWin.h"

extern "C" int fgetc(FILE *stream)
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
