// Spec: ISO C 7.19.4.3 - tmpfile: a binary file opened "wb+", removed when it is closed or the program ends.
// Its name is the stream's slot's (Stream::temporaryName), in the current folder.
#include "StreamWin.h"

extern "C" FILE *tmpfile(void)
{
    FILE *f = grt::freeStream();
    if (!f) return 0;
    char name[24];
    rts6x::Stream::temporaryName(f->slot, name);
    f = rts6x::Stream::open(name, "wb+", f);
    if (f) f->flags |= rts6x::Stream::Temporary;
    return f;
}
