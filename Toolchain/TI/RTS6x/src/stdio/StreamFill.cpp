// Spec: ISO C 7.19.9.4 (ftell on a text stream: a value fseek takes back to the same character) and
// 7.19.2/2 (a text stream's characters need not be the file's bytes). CCS 5.5's Windows host reads a
// text file as CR LF to LF and answers lseek in bytes; read-ahead is counted here as the host's bytes.

#include "Stream.h"
#include "../host/CioChannel.h"

namespace rts6x {

long Stream::fillAt_[RTS6X_FTABLE_COUNT];
int Stream::fillCount_[RTS6X_FTABLE_COUNT];

void Stream::recordFill(long at, int count)
{
    int slot = (int)(f_ - _ftable);
    fillAt_[slot] = at;
    fillCount_[slot] = at < 0 ? -1 : count;
}

long Stream::hostPosition(long end)
{
    long ahead = (long)(f_->bufend - f_->pos);
    int slot = (int)(f_ - _ftable), count = fillCount_[slot];
    long start = fillAt_[slot];
    // A byte for each character: no fill to measure, a binary stream, or a fill the host did not translate.
    if (!ahead || count < 0 || end - start == count) return end - ahead;
    long taken = count - ahead;
    if (taken <= 0) return start + taken;
    // The fill read again up to the next character, into what the program has already taken.
    if (CioChannel::seek(f_->fd, start, CioChannel::FromStart) < 0) return -1L;
    long got = 0;
    while (got < taken) {
        int n = CioChannel::read(f_->fd, (char *)f_->buf + got, (unsigned)(taken - got));
        if (n <= 0) break;
        got += n;
    }
    long here = got == taken ? CioChannel::seek(f_->fd, 0, CioChannel::FromHere) : -1L;
    return CioChannel::seek(f_->fd, end, CioChannel::FromStart) < 0 ? -1L : here;
}

}  // namespace rts6x
