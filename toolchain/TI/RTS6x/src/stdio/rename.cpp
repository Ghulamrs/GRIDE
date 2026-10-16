// Spec: ISO C 7.19.4.2 - rename: 0, or nonzero if the file was not renamed.

#include <stdio.h>
#include "../host/CioChannel.h"

extern "C" int rename(const char *from, const char *to)
{
    return rts6x::CioChannel::rename(from, to) == 0 ? 0 : -1;
}
