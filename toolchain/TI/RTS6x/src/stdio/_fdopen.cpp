// Spec: Microsoft C runtime, _fdopen - a stream over the open descriptor fd, in mode as fopen reads it, its
// translation the descriptor's: the stream, or null with errno EBADF (fd not open) or EINVAL (the mode). Each runtime
// opens it its own way (Stream::adopt): grt's descriptors, RTS6x's host channel.

#include <stdio.h>
#include "Stream.h"

extern "C" FILE *_fdopen(int fd, const char *mode)
{
    return rts6x::Stream::adopt(fd, mode);
}