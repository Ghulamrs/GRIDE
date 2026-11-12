// Spec: Microsoft C runtime, _dup2 - descriptor to made a second name for fd's host descriptor, closed first if it
// was open: 0, or -1 with errno EBADF.
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _dup2(int fd, int to)
{
    using rts6x::DescriptorModes;
    if (DescriptorModes::modeOf(fd) == 0 || to < 0 || to >= DescriptorModes::Count) {
        ErrorNumber::set(EBADF);
        return -1;
    }
    if (fd == to) return 0;
    if (DescriptorModes::modeOf(to) != 0) _close(to);
    DescriptorModes::share(to, fd);
    return 0;
}