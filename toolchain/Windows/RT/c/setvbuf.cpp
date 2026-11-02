// Spec: ISO C 7.19.5.6 and 7.19.5.5 - setvbuf and setbuf, as the Microsoft runtime's: a mode other than _IOFBF,
// _IOLBF and _IONBF, or a buffered mode with a size outside 2..INT_MAX, is an invalid parameter (the fast fail of a
// Release program); _IOLBF buffers as _IOFBF does; the size is taken down to an even number; bytes waiting are
// written first. setbuf is setvbuf with BUFSIZ, or _IONBF for a null buffer.
#include <limits.h>
#include <stdlib.h>
#include "exit/Termination.h"
#include "StreamWin.h"

extern "C" int setvbuf(FILE *stream, char *buffer, int mode, size_t size)
{
    grt::valid(stream);
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) __grt_fail_fast();
    if (mode != _IONBF && (size < 2 || size > INT_MAX)) __grt_fail_fast();
    bool held = grt::lock(stream);
    grt::flush(stream);
    if (stream->flags & rts6x::Stream::OwnBuffer) free(stream->buf);
    stream->buf = stream->pos = stream->bufend = stream->buff_stop = 0;
    stream->flags &= ~(unsigned)(rts6x::Stream::OwnBuffer | grt::Pending | grt::Unbuffered);
    int result = 0;
    if (mode == _IONBF) {
        stream->flags |= grt::Unbuffered;
    } else {
        size &= ~(size_t)1;
        unsigned char *b = (unsigned char *)buffer;
        if (!b) {
            b = (unsigned char *)malloc(size);
            if (b) stream->flags |= rts6x::Stream::OwnBuffer;
            else result = -1;
        }
        if (b) {
            stream->buf = stream->pos = stream->bufend = b;
            stream->buff_stop = b + size;
            rts6x::Termination::closeStreamsWith(grt::closeAll);
        }
    }
    grt::unlock(stream, held);
    return result;
}

extern "C" void setbuf(FILE *stream, char *buffer)
{
    if (buffer) setvbuf(stream, buffer, _IOFBF, BUFSIZ);
    else setvbuf(stream, 0, _IONBF, 0);
}
