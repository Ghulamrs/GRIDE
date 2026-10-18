// Spec: ISO C 7.19.5.4 - freopen closes the stream, then opens the file into the same FILE; a null
// path (changing the mode alone) is a request the standard lets an implementation refuse.

#include "Stream.h"

extern "C" FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    rts6x::Stream old(stream);
    old.close();
    return path ? rts6x::Stream::open(path, mode, stream) : 0;
}
