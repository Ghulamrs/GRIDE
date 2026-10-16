// Spec: ISO C 7.19.7.1 (fgetc), 7.19.7.11 (ungetc: one character back, the end-of-file indicator
// cleared) and 7.19.8.1 (fread). The host is asked for a buffer's worth at a time - one byte at a
// time on standard input, which a person may be typing.

#include <stdlib.h>
#include <string.h>
#include "Stream.h"
#include "../host/CioChannel.h"

namespace rts6x {

bool Stream::ensureBuffer()
{
    if (f_->buf) return true;
    unsigned char *b = (unsigned char *)malloc(Capacity);
    if (!b) {
        f_->flags |= Failed;
        return false;
    }
    f_->buf = f_->pos = f_->bufend = b;
    f_->buff_stop = b + Capacity;
    f_->flags |= OwnBuffer;
    return true;
}

int Stream::get()
{
    return f_->pos < f_->bufend ? *f_->pos++ : getSlow();
}

int Stream::getSlow()
{
    if (!(f_->flags & Readable)) {
        f_->flags |= Failed;
        return EOF;
    }
    if ((f_->flags & AtEnd) || !ensureBuffer()) return EOF;
    // A text file's fill is measured, for ftell: the host may give fewer characters than it passed bytes.
    bool text = f_->fd > 2 && !(f_->flags & Binary);
    long at = text ? CioChannel::seek(f_->fd, 0, CioChannel::FromHere) : 0;
    int n = CioChannel::read(f_->fd, (char *)f_->buf, f_->fd == 0 ? 1u : (unsigned)Capacity);
    if (text) recordFill(at, n);
    if (n <= 0) {
        f_->flags |= n == 0 ? AtEnd : Failed;
        return EOF;
    }
    f_->pos = f_->buf;
    f_->bufend = f_->buf + n;
    return *f_->pos++;
}

int Stream::unget(int c)
{
    if (c == EOF || !ensureBuffer()) return EOF;
    if (f_->pos == f_->buf) {
        if (f_->bufend == f_->buff_stop) return EOF;
        memmove(f_->buf + 1, f_->buf, (size_t)(f_->bufend - f_->buf));
        f_->bufend++;
        f_->pos++;
    }
    *--f_->pos = (unsigned char)c;
    f_->flags &= ~(unsigned)AtEnd;
    return (unsigned char)c;
}

size_t Stream::read(char *dst, size_t n)
{
    size_t done = 0;
    while (done < n) {
        size_t held = (size_t)(f_->bufend - f_->pos);
        if (held) {
            size_t take = held < n - done ? held : n - done;
            memcpy(dst + done, f_->pos, take);
            f_->pos += take;
            done += take;
            continue;
        }
        int c = getSlow();
        if (c == EOF) break;
        dst[done++] = (char)c;
    }
    return done;
}

}  // namespace rts6x
