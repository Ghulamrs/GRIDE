// Spec: ISO C 7.19.5.3 - fopen: the file named, opened in the mode, as a stream; a null pointer if not.

#include "Stream.h"

extern "C" FILE *fopen(const char *path, const char *mode)
{
    return rts6x::Stream::open(path, mode, 0);
}
