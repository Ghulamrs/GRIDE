// Spec: ISO C 7.19.9.4 - ftell: the position, or -1L.

#include "Stream.h"

extern "C" long ftell(FILE *stream)
{
    return rts6x::Stream(stream).tell();
}
