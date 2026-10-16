// Spec: POSIX fileno and Microsoft _fileno - the host descriptor behind the stream.

#include <stdio.h>

extern "C" int fileno(FILE *stream)
{
    return stream->fd;
}

extern "C" int _fileno(FILE *stream)
{
    return stream->fd;
}
