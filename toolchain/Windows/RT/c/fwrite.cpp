// Spec: ISO C 7.19.8.2 - fwrite: count objects of size bytes; the number of whole objects written. RTS6x's, with
// the stream locked for the call (StreamWin.h).
#include "host/CioChannel.h"
#include "StreamWin.h"

extern "C" size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    if (size == 0 || count == 0) return 0;
    grt::valid(stream);
    bool held = grt::lock(stream);
    rts6x::Stream to(stream);
    int fd = to.writeDescriptor();
    int n = fd < 0 ? -1 : rts6x::CioChannel::write(fd, (const char *)ptr, (unsigned)(size * count));
    if (n < 0) to.wrote(n);
    grt::unlock(stream, held);
    return n < 0 ? 0 : (size_t)n / size;
}
