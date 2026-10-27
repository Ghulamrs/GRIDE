// Spec: ISO C 7.19.8.1 - fread: up to count objects of size bytes; the number of whole objects read. RTS6x's,
// with the stream locked for the call (StreamWin.h).
#include "StreamWin.h"

extern "C" size_t fread(void *ptr, size_t size, size_t count, FILE *stream)
{
    if (size == 0 || count == 0) return 0;
    grt::valid(stream);
    bool held = grt::lock(stream);
    size_t n = rts6x::Stream(stream).read((char *)ptr, size * count);
    grt::unlock(stream, held);
    return n / size;
}
