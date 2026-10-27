// Spec: ISO C 7.19.5.4 - freopen closes the stream, then opens the file into the same FILE; a null path (changing
// the mode alone) is a request the standard lets an implementation refuse. RTS6x's, with the stream locked for the
// call (StreamWin.h), and kept Claimed between the close and the open so that no fopen takes it in between.
#include "StreamWin.h"

extern "C" FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    grt::closeStream(stream, path ? (unsigned)grt::Claimed : 0u);
    FILE *f = path ? rts6x::Stream::open(path, mode, stream) : 0;
    grt::unlock(stream, held);
    return f;
}
