// Spec: ISO C 7.19.5.2 - fflush: a stream's unwritten bytes written; with a null pointer, every stream's.
// 0, or EOF with the error indicator set. A stream being read is left as it is, as the Microsoft runtime
// leaves it. Each stream is flushed under its lock, every stream's under the table's first (StreamWin.h); a stream
// with nothing waiting is passed over without its lock, as Microsoft's is, so a thread blocked reading stdin does
// not hold up a flush of everything.
#include "StreamWin.h"

extern "C" int fflush(FILE *stream)
{
    if (stream) {
        if (!(stream->flags & grt::Pending)) return 0;
        bool held = grt::lock(stream);
        int result = grt::flush(stream);
        grt::unlock(stream, held);
        return result;
    }
    int result = 0;
    bool all = __grt_threads != 0;
    if (all) __grt_lock(&grt::streamsLock);
    for (int i = 0; i < GRT_STREAMS; i++) {
        FILE *f = __grt_streams[i];
        if (!f || !(f->flags & grt::Pending)) continue;
        bool held = grt::lock(f);
        if ((f->flags & rts6x::Stream::Open) && grt::flush(f) != 0) result = EOF;
        grt::unlock(f, held);
    }
    if (all) __grt_unlock(&grt::streamsLock);
    return result;
}
