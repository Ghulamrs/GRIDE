// Spec: ISO C 7.19.9.2 - fseek: 0, or nonzero for a request that cannot be satisfied.

#include "Stream.h"

extern "C" int fseek(FILE *stream, long offset, int whence)
{
    return rts6x::Stream(stream).seek(offset, whence);
}
