// Spec: Microsoft C runtime, _write - count bytes: the count, or -1 with errno EBADF (not open) or ENOSPC (the host
// wrote none).
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _write(int fd, const void *buffer, unsigned int count)
{
    if (rts6x::DescriptorModes::modeOf(fd) == 0) {
        ErrorNumber::set(EBADF);
        return -1;
    }
    if (count == 0) return 0;
    int wrote = rts6x::CioChannel::write(fd, static_cast<const char *>(buffer), count);
    if (wrote < 0) ErrorNumber::set(ENOSPC);
    return wrote;
}