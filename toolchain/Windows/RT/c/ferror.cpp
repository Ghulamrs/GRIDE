// Spec: ISO C 7.19.10.3 - ferror: nonzero if the error indicator is set. RTS6x's, with the Microsoft
// runtime's check of the stream (StreamWin.h): a null FILE * fails fast. One word is read, so no lock is taken,
// as the UCRT's ferror takes none.
#include "StreamWin.h"

extern "C" int ferror(FILE *stream)
{
    grt::valid(stream);
    return rts6x::Stream(stream).failed();
}
