// Spec: Microsoft C runtime, _read - up to count bytes, the host's request at a time (1 KB) until a short one, the
// console's a line at a time: the count, 0 at the end, or -1 with errno EBADF.
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _read(int fd, void *buffer, unsigned int count)
{
    if (rts6x::DescriptorModes::modeOf(fd) == 0) {
        ErrorNumber::set(EBADF);
        return -1;
    }
    char *at = static_cast<char *>(buffer);
    unsigned done = 0;
    while (done < count) {
        unsigned ask = count - done;
        if (ask > rts6x::CioChannel::DataCapacity) ask = rts6x::CioChannel::DataCapacity;
        int got = rts6x::CioChannel::read(fd, at + done, ask);
        if (got < 0) {
            if (done != 0) break;
            ErrorNumber::set(EBADF);
            return -1;
        }
        done += static_cast<unsigned>(got);
        if (static_cast<unsigned>(got) < ask || rts6x::DescriptorModes::host(fd) <= 2) break;
    }
    return static_cast<int>(done);
}