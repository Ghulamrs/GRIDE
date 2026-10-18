// Spec: ISO C 7.19.7.5 - getc is fgetc.

#include "Stream.h"

extern "C" int getc(FILE *stream)
{
    return rts6x::Stream(stream).get();
}
