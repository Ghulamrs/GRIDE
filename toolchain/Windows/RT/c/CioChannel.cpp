// Spec: RTS6x's host\CioChannel.h - the one door RTS6x's C library has to its host: open, close, read,
// write, seek, unlink, rename, the environment, the time and a clock. On the C6747 each is a request through
// the C$$IO$$ buffer; on Windows each is a call into the Windows layer (win\files.c, environment.c, time.c),
// which keeps RTS6x's numbering of descriptors, flags and origins. A write to a stream's descriptor
// (StreamWin.h's StreamDescriptor | slot, what Stream::writeDescriptor answers) goes into that stream's buffer.
#include "host/CioChannel.h"
#include "StreamWin.h"

namespace rts6x {
int CioChannel::write(int fd, const char *bytes, unsigned count)
{
    if (fd & grt::StreamDescriptor) return grt::put(__grt_streams[fd & 0xFFFF], bytes, count);
    return __grt_write(fd, bytes, count);
}
int CioChannel::read(int fd, char *bytes, unsigned count) { return __grt_read(fd, bytes, count); }
int CioChannel::open(const char *path, unsigned flags) { return __grt_open(path, flags); }
int CioChannel::close(int fd) { return __grt_close(fd); }
long CioChannel::seek(int fd, long offset, Origin origin) { return __grt_seek(fd, offset, (int)origin); }
int CioChannel::unlink(const char *path) { return __grt_unlink(path); }
int CioChannel::rename(const char *from, const char *to) { return __grt_rename(from, to); }
bool CioChannel::environment(const char *name, char *value, unsigned capacity)
{
    return __grt_getenv(name, value, capacity) != 0;
}
unsigned long CioChannel::hostTime() { return (unsigned long)__grt_time_now(); }
unsigned long CioChannel::cycles() { return (unsigned long)__grt_clock_ms(); }
}  // namespace rts6x
