// Spec: ISO C 7.19.7.1 - fgetc: the next byte as an unsigned char, or EOF.

#include "Stream.h"

extern "C" int fgetc(FILE *stream)
{
    return rts6x::Stream(stream).get();
}
