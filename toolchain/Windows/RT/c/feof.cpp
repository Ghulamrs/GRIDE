// Spec: ISO C 7.19.10.2 - feof: nonzero if the end-of-file indicator is set. RTS6x's, with the Microsoft
// runtime's check of the stream (StreamWin.h): a null FILE * fails fast. One word is read, so no lock is taken,
// as the UCRT's feof takes none.
#include "StreamWin.h"

extern "C" int feof(FILE *stream)
{
    grt::valid(stream);
    return rts6x::Stream(stream).atEnd();
}
