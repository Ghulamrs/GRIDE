// Spec: ISO C 7.19.8.2 - fwrite: count objects of size bytes; the number of whole objects written.

#include "Stream.h"
#include "../host/CioChannel.h"

extern "C" size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
    if (size == 0 || count == 0) return 0;
    rts6x::Stream to(stream);
    int fd = to.writeDescriptor();
    int n = fd < 0 ? -1 : rts6x::CioChannel::write(fd, (const char *)ptr, (unsigned)(size * count));
    if (n < 0) {
        to.wrote(n);
        return 0;
    }
    return (size_t)n / size;
}
