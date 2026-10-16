// Spec: ISO C 7.19.10.2 - feof: nonzero if the end-of-file indicator is set.

#include "Stream.h"

extern "C" int feof(FILE *stream)
{
    return rts6x::Stream(stream).atEnd();
}
