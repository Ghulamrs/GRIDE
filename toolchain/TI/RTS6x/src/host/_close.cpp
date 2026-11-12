// Spec: Microsoft C runtime, _close - 0, or -1 with errno EBADF. A standard descriptor (the host's console) is let go
// of here and not closed at the host, as the streams leave it.
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _close(int fd)
{
    if (rts6x::DescriptorModes::modeOf(fd) == 0) {
        ErrorNumber::set(EBADF);
        return -1;
    }
    if (rts6x::DescriptorModes::host(fd) <= 2 && !rts6x::DescriptorModes::shared(fd)) {
        rts6x::DescriptorModes::closed(fd);
        return 0;
    }
    if (rts6x::CioChannel::close(fd) < 0) {
        ErrorNumber::set(EBADF);
        return -1;
    }
    return 0;
}