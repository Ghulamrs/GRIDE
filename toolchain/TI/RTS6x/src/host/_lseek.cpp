// Spec: Microsoft C runtime, _lseek - the new position from the start: -1 with errno EBADF (not open) or EINVAL (an
// origin past SEEK_END, or one the host refused).
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" long _lseek(int fd, long offset, int origin)
{
    if (rts6x::DescriptorModes::modeOf(fd) == 0) {
        ErrorNumber::set(EBADF);
        return -1;
    }
    if (origin < 0 || origin > 2) {
        ErrorNumber::set(EINVAL);
        return -1;
    }
    long at = rts6x::CioChannel::seek(fd, offset, static_cast<rts6x::CioChannel::Origin>(origin));
    if (at < 0) ErrorNumber::set(EINVAL);
    return at < 0 ? -1L : at;
}