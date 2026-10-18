// Spec: ISO C 7.19.9.5 - rewind is fseek to the start with the error indicator cleared as well.

#include "Stream.h"

extern "C" void rewind(FILE *stream)
{
    rts6x::Stream s(stream);
    s.seek(0, SEEK_SET);
    s.clear();
}
