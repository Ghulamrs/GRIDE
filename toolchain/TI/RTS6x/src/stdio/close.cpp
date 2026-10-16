// Spec: POSIX close - the host descriptor closed: 0, or -1. A program closes 2 to keep a runtime's
// own words off its error stream; the host keeps 0-2 open, and RTS6x's terminate writes nothing.

#include "../host/CioChannel.h"

extern "C" int close(int fd)
{
    return rts6x::CioChannel::close(fd) < 0 ? -1 : 0;
}
