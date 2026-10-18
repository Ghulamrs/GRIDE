// Spec: ISO C 7.19.7.11 - ungetc pushes c back to be read next; c, or EOF if it cannot.

#include "Stream.h"

extern "C" int ungetc(int c, FILE *stream)
{
    return rts6x::Stream(stream).unget(c);
}
