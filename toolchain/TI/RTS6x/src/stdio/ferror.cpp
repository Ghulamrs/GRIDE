// Spec: ISO C 7.19.10.3 - ferror: nonzero if the error indicator is set.

#include "Stream.h"

extern "C" int ferror(FILE *stream)
{
    return rts6x::Stream(stream).failed();
}
