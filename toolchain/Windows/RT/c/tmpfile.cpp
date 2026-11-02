// Spec: ISO C 7.19.4.3 - tmpfile: a binary file opened "wb+", removed when it is closed or the program ends. As the
// Microsoft runtime's: a new file in the temporary folder (win\temporary.c), opened for Windows to delete when its
// handle closes, so a program that ends any way at all leaves none behind.
#include "StreamWin.h"

extern "C" FILE *tmpfile(void)
{
    char name[300];
    if (__grt_temporary_name('t', name, sizeof name) != 0) return 0;
    FILE *f = rts6x::Stream::open(name, "w+bxD", 0);
    if (f) f->flags |= rts6x::Stream::Temporary;
    return f;
}
