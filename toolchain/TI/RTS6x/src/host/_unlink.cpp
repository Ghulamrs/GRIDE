// Spec: Microsoft C runtime, _unlink - path removed: 0, or -1 with errno ENOENT (the host gives no reason).
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _unlink(const char *path)
{
    if (path == 0 || rts6x::CioChannel::unlink(path) < 0) {
        ErrorNumber::set(path == 0 ? EINVAL : ENOENT);
        return -1;
    }
    return 0;
}