// Spec: Microsoft C runtime, _dup - a second descriptor, the lowest free from 3, sharing fd's host descriptor and
// mode: it, or -1 with errno EBADF or EMFILE.
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _dup(int fd)
{
    if (rts6x::DescriptorModes::modeOf(fd) == 0) {
        ErrorNumber::set(EBADF);
        return -1;
    }
    int to = rts6x::DescriptorModes::vacant();
    if (to < 0) {
        ErrorNumber::set(EMFILE);
        return -1;
    }
    rts6x::DescriptorModes::share(to, fd);
    return to;
}