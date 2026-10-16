// Spec: ISO C 7.19.9.2 (fseek: SEEK_SET, SEEK_CUR, SEEK_END; the end-of-file indicator cleared,
// pushed-back characters dropped) and 7.19.9.4 (ftell: the position, or -1L). The host keeps the position;
// read-ahead lies between it and the program's, counted in host bytes (StreamFill.cpp) so fseek returns.

#include "Stream.h"
#include "../host/CioChannel.h"

namespace rts6x {

int Stream::seek(long offset, int whence)
{
    if (whence < SEEK_SET || whence > SEEK_END) return -1;
    if (whence == SEEK_CUR) {
        long here = tell();
        if (here < 0) return -1;
        offset += here;
        whence = SEEK_SET;
    }
    // The host first: a refused seek leaves the stream where it was, read-ahead and all.
    if (CioChannel::seek(f_->fd, offset, (CioChannel::Origin)whence) < 0) return -1;
    f_->pos = f_->bufend = f_->buf;
    forgetFill();
    f_->flags &= ~(unsigned)AtEnd;
    return 0;
}

long Stream::tell()
{
    long host = CioChannel::seek(f_->fd, 0, CioChannel::FromHere);
    return host < 0 ? -1L : hostPosition(host);
}

}  // namespace rts6x
