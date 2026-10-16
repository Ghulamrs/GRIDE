// Spec: ISO C 7.19.10.1 - clearerr clears both indicators.

#include "Stream.h"

extern "C" void clearerr(FILE *stream)
{
    rts6x::Stream s(stream);
    s.clear();
}
