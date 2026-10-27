// StreamCore.cpp - the Windows stream layer's buffer: RTS6x's Stream members that read, write and position
// (stdio\Stream.h), over grt's descriptors (win\files.c), and the streams' locks. StreamWin.h says how the buffer
// is kept and who locks it.
//
// Spec: ISO C 7.19.3 (buffering), 7.19.5.3/6 (between output and input a flush or a positioning call),
// 7.19.9 (positions); the Microsoft runtime's text mode (LF written as CR LF, CR LF read as LF, CTRL+Z the end
// of a file but a character on a device) and its buffer of 4,096 bytes.
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "StreamWin.h"
#include "exit/Termination.h"

using rts6x::Stream;

namespace grt {

bool lock(FILE *f)
{
    if (!__grt_threads) return false;
    __grt_lock(&f->lock);
    return true;
}

void unlock(FILE *f, bool held)
{
    if (held) __grt_unlock(&f->lock);
}

bool ensureBuffer(FILE *f)
{
    if (f->buf) return true;
    unsigned char *b = (unsigned char *)malloc(BufferSize);
    if (!b) return false;
    f->buf = f->pos = f->bufend = b;
    f->buff_stop = b + BufferSize;
    f->flags |= Stream::OwnBuffer;
    rts6x::Termination::closeStreamsWith(closeAll);
    return true;
}

int flush(FILE *f)
{
    if (!(f->flags & Pending)) return 0;
    size_t n = (size_t)(f->pos - f->buf);
    f->pos = f->bufend = f->buf;
    if (n != 0 && __grt_write_raw(f->fd, (const char *)f->buf, (unsigned)n) < 0) {
        f->flags |= Stream::Failed;
        return EOF;
    }
    return 0;
}

// Reading stopped for writing: the raw bytes read ahead given back to a file (a pipe's are dropped).
static void giveBack(FILE *f)
{
    long long ahead = (long long)(f->bufend - f->pos);
    if (ahead != 0) __grt_seek64(f->fd, -ahead, SEEK_CUR);
    f->pos = f->bufend = f->buf;
}

int put(FILE *f, const char *s, unsigned count)
{
    valid(f);
    int info = __grt_descriptor(f->fd);
    if (info < 0 || !(f->flags & Stream::Writable)) {
        f->flags |= Stream::Failed;
        return -1;
    }
    if (f->pos < f->bufend) giveBack(f);
    bool text = (info & GRT_TEXT) != 0;
    if ((f->flags & Unbuffered) || (info & GRT_KIND_MASK) == GRT_KIND_CONSOLE || !ensureBuffer(f)) {
        if (flush(f) != 0) return -1;
        f->flags &= ~(unsigned)Pending;
        if (__grt_write(f->fd, s, count) < 0) {
            f->flags |= Stream::Failed;
            return -1;
        }
        return (int)count;
    }
    if (!(f->flags & Pending)) {
        f->pos = f->bufend = f->buf;
        f->flags |= Pending;
    }
    if (text) f->flags |= TextOut;
    else f->flags &= ~(unsigned)TextOut;
    const char *end = s + count;
    if (!text) {
        // A run as large as the buffer, the buffer empty, goes straight to Windows, as the Microsoft runtime's.
        if (f->pos == f->buf && count >= (unsigned)BufferSize) {
            if (__grt_write_raw(f->fd, s, count) < 0) {
                f->flags |= Stream::Failed;
                return -1;
            }
            return (int)count;
        }
        while (s < end) {
            if (f->pos == f->buff_stop && flush(f) != 0) return -1;
            size_t room = (size_t)(f->buff_stop - f->pos), n = (size_t)(end - s);
            if (n > room) n = room;
            memcpy(f->pos, s, n);
            f->pos += n;
            s += n;
        }
        return (int)count;
    }
    while (s < end) {
        if (f->buff_stop - f->pos < 2 && flush(f) != 0) return -1;
        size_t room = (size_t)(f->buff_stop - f->pos), left = (size_t)(end - s);
        size_t look = left < room ? left : room;
        const char *lf = (const char *)memchr(s, '\n', look);
        size_t run = lf ? (size_t)(lf - s) : look;
        memcpy(f->pos, s, run);
        f->pos += run;
        s += run;
        if (lf && f->buff_stop - f->pos >= 2) {
            f->pos[0] = '\r';
            f->pos[1] = '\n';
            f->pos += 2;
            s++;
        }
    }
    return (int)count;
}

int putLocked(int c, FILE *f)
{
    bool held = lock(f);
    int result;
    if ((f->flags & Pending) && f->pos < f->buff_stop && (c != '\n' || !(f->flags & TextOut))) {
        *f->pos++ = (unsigned char)c;
        result = (unsigned char)c;
    } else {
        char ch = (char)c;
        result = put(f, &ch, 1) < 0 ? EOF : (unsigned char)c;
    }
    unlock(f, held);
    return result;
}

int getLocked(FILE *f)
{
    bool held = lock(f);
    int c;
    if (f->pos < f->bufend && (!(f->flags & TextIn) || (*f->pos != '\r' && *f->pos != CtrlZ))) c = *f->pos++;
    else c = getSlow(f);
    unlock(f, held);
    return c;
}

int getSlow(FILE *f)
{
    valid(f);
    if (!(f->flags & Stream::Readable)) {
        f->flags |= Stream::Failed;
        return EOF;
    }
    if (f->flags & Pending) {
        if (flush(f) != 0) return EOF;
        f->flags &= ~(unsigned)(Pending | TextOut);
    }
    for (;;) {
        if (f->pos < f->bufend) {
            unsigned char c = *f->pos;
            if (!(f->flags & TextIn) || (c != '\r' && c != CtrlZ)) {
                f->pos++;
                return c;
            }
            if (c == CtrlZ) {
                if (f->flags & Device) {
                    f->pos++;
                    return c;
                }
                f->flags |= Stream::AtEnd;
                return EOF;
            }
            if (f->pos + 1 < f->bufend) {
                if (f->pos[1] == '\n') {
                    f->pos += 2;
                    return '\n';
                }
                f->pos++;
                return '\r';
            }
            // A CR ends the bytes read: the next byte decides whether the two are one LF.
            f->buf[0] = '\r';
            f->pos = f->buf;
            f->bufend = f->buf + 1;
            int n = __grt_read_raw(f->fd, (char *)f->buf + 1, (unsigned)(f->buff_stop - f->buf - 1));
            if (n > 0) {
                f->bufend += n;
                continue;
            }
            f->pos++;
            return '\r';
        }
        if (f->flags & Stream::AtEnd) return EOF;
        int info = __grt_descriptor(f->fd);
        if (info < 0 || !ensureBuffer(f)) {
            f->flags |= Stream::Failed;
            return EOF;
        }
        f->flags &= ~(unsigned)(TextIn | Device);
        if (info & GRT_TEXT) f->flags |= TextIn;
        if ((info & GRT_KIND_MASK) != GRT_KIND_DISK && (info & GRT_KIND_MASK) != GRT_KIND_PIPE) f->flags |= Device;
        f->pos = f->bufend = f->buf;
        int n = __grt_read_raw(f->fd, (char *)f->buf, (unsigned)(f->buff_stop - f->buf));
        if (n <= 0) {
            f->flags |= n == 0 ? Stream::AtEnd : Stream::Failed;
            return EOF;
        }
        f->bufend = f->buf + n;
    }
}

}  // namespace grt

namespace rts6x {

int Stream::get()
{
    FILE *f = f_;
    grt::valid(f);
    if (f->pos < f->bufend) {
        unsigned char c = *f->pos;
        if (!(f->flags & grt::TextIn) || (c != '\r' && c != grt::CtrlZ)) {
            f->pos++;
            return c;
        }
    }
    return grt::getSlow(f);
}

int Stream::getSlow()
{
    return grt::getSlow(f_);
}

int Stream::unget(int c)
{
    FILE *f = f_;
    grt::valid(f);
    if (c == EOF) return EOF;
    if (f->flags & grt::Pending) {
        if (grt::flush(f) != 0) return EOF;
        f->flags &= ~(unsigned)(grt::Pending | grt::TextOut);
    }
    if (!grt::ensureBuffer(f)) return EOF;
    if (f->pos == f->buf) {
        if (f->bufend == f->buff_stop) return EOF;
        memmove(f->buf + 1, f->buf, (size_t)(f->bufend - f->buf));
        f->bufend++;
        f->pos++;
    }
    *--f->pos = (unsigned char)c;
    f->flags &= ~(unsigned)AtEnd;
    return (unsigned char)c;
}

size_t Stream::read(char *dst, size_t n)
{
    FILE *f = f_;
    grt::valid(f);
    size_t done = 0;
    while (done < n) {
        size_t held = f->pos < f->bufend ? (size_t)(f->bufend - f->pos) : 0, want = n - done;
        if (held != 0) {
            size_t take = held < want ? held : want, i = 0;
            if (f->flags & grt::TextIn) {
                while (i < take && f->pos[i] != '\r' && f->pos[i] != grt::CtrlZ) i++;
            } else {
                i = take;
            }
            memcpy(dst + done, f->pos, i);
            f->pos += i;
            done += i;
            if (i == take) continue;
        } else if (want >= (size_t)grt::BufferSize && !(f->flags & (grt::Pending | AtEnd)) &&
                   (f->flags & Readable)) {
            // A large binary read, nothing ahead: straight from Windows into the caller's memory.
            int info = __grt_descriptor(f->fd);
            if (info >= 0 && (info & GRT_BINARY)) {
                unsigned ask = want > 0x40000000u ? 0x40000000u : (unsigned)want;
                int got = __grt_read_raw(f->fd, dst + done, ask);
                if (got <= 0) {
                    f->flags |= got == 0 ? AtEnd : Failed;
                    break;
                }
                done += (size_t)got;
                continue;
            }
        }
        int c = grt::getSlow(f);
        if (c == EOF) break;
        dst[done++] = (char)c;
    }
    return done;
}

int Stream::writeDescriptor()
{
    grt::valid(f_);
    if (!(f_->flags & Writable)) {
        f_->flags |= Failed;
        return -1;
    }
    return grt::StreamDescriptor | f_->slot;
}

int Stream::seek(long offset, int whence)
{
    FILE *f = f_;
    grt::valid(f);
    if (whence < SEEK_SET || whence > SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    long long target = offset;
    if (f->flags & grt::Pending) {
        if (grt::flush(f) != 0) return -1;
    } else if (whence == SEEK_CUR) {
        target -= (long long)(f->bufend - f->pos);
    }
    if (__grt_seek64(f->fd, target, whence) < 0) return -1;
    f->pos = f->bufend = f->buf;
    f->flags &= ~(unsigned)(AtEnd | grt::Pending | grt::TextOut);
    return 0;
}

long Stream::tell()
{
    FILE *f = f_;
    grt::valid(f);
    long long here = __grt_seek64(f->fd, 0, SEEK_CUR);
    if (here < 0) return -1L;
    if (f->flags & grt::Pending) here += (long long)(f->pos - f->buf);
    else here -= (long long)(f->bufend - f->pos);
    if (here > 0x7FFFFFFFLL) {
        errno = EINVAL;
        return -1L;
    }
    return (long)here;
}

}  // namespace rts6x
