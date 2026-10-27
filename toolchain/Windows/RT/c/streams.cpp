// Spec: the Microsoft runtime's names for what the compilers' Windows headers reach: stdin, stdout and stderr
// are __acrt_iob_func(0), (1) and (2), and _fileno is the descriptor behind a stream.
#include "rts6x.h"

extern "C" FILE *__acrt_iob_func(unsigned which)
{
    return &_ftable[which];
}

extern "C" int _fileno(FILE *stream)
{
    return stream->fd;
}
