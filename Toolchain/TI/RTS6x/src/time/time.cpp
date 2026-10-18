// Spec: ISO C 7.23.2.4 - time: the host's calendar time in seconds since 1970, also stored in *t.

#include <time.h>
#include "../host/CioChannel.h"

extern "C" time_t time(time_t *t)
{
    time_t now = (time_t)rts6x::CioChannel::hostTime();
    if (t) *t = now;
    return now;
}
