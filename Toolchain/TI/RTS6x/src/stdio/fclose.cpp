// Spec: ISO C 7.19.5.1 - fclose: the stream closed and its buffer freed; 0, or EOF on failure.

#include "Stream.h"

extern "C" int fclose(FILE *stream)
{
    return rts6x::Stream(stream).close();
}
