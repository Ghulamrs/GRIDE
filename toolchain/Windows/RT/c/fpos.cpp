// Spec: ISO C 7.19.9.1 and 7.19.9.3 - fgetpos and fsetpos, an fpos_t being the Microsoft runtime's 64-bit offset;
// _fseeki64 and _ftelli64, fseek and ftell past 2 GB. The stream locked for the call (StreamWin.h).
#include "StreamWin.h"

extern "C" int _fseeki64(FILE *stream, long long offset, int whence)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    int result = grt::seek(stream, offset, whence);
    grt::unlock(stream, held);
    return result;
}

extern "C" long long _ftelli64(FILE *stream)
{
    grt::valid(stream);
    bool held = grt::lock(stream);
    long long result = grt::tell(stream);
    grt::unlock(stream, held);
    return result;
}

extern "C" int fgetpos(FILE *stream, fpos_t *position)
{
    long long here = _ftelli64(stream);
    if (here < 0) return -1;
    *position = here;
    return 0;
}

extern "C" int fsetpos(FILE *stream, const fpos_t *position)
{
    return _fseeki64(stream, *position, SEEK_SET);
}
