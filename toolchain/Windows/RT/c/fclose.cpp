// Spec: ISO C 7.19.5.1 - fclose: the stream closed and its buffer freed; 0, or EOF on failure. RTS6x's, with the
// stream locked for the call (StreamWin.h); the slot is free for another fopen from the close's last store.
#include "StreamWin.h"

extern "C" int fclose(FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    int result = grt::closeStream(stream, 0);
    grt::unlock(stream, held);
    return result;
}
