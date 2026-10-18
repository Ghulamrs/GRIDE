// Spec: ISO C 7.19.4.1 - remove: 0, or nonzero if the file could not be removed.

#include <stdio.h>
#include "../host/CioChannel.h"

extern "C" int remove(const char *path)
{
    return rts6x::CioChannel::unlink(path) == 0 ? 0 : -1;
}
