// Spec: ISO C 7.19.4.3 - tmpfile: a file opened "wb+", removed when it is closed or the program ends
// (Stream::open has exit close every open stream, and closing a temporary one removes its file).

#include "Stream.h"

extern "C" FILE *tmpfile(void)
{
    for (int i = rts6x::Stream::FirstFree; i < RTS6X_FTABLE_COUNT; i++) {
        if (_ftable[i].flags & rts6x::Stream::Open) continue;
        char name[24];
        rts6x::Stream::temporaryName(i, name);
        FILE *f = rts6x::Stream::open(name, "wb+", &_ftable[i]);
        if (!f) return 0;
        f->flags |= rts6x::Stream::Temporary;
        return f;
    }
    return 0;
}
