// StreamOpen.cpp - the Windows stream layer's streams: opening, closing, the slots and the end of the program.
//
// Spec: ISO C 7.19.5.3 (fopen's modes), 7.19.5.1 (fclose: the buffer written, the file closed), 7.20.4.3/4
// (exit: every open stream flushed and closed); the Microsoft runtime's 512 streams. stdin, stdout and stderr
// are RTS6x's _ftable (stdio\ftable.cpp); the others are allocated as fopen needs them and kept for reuse, so
// a FILE pointer stays valid memory after fclose and freopen reopens the same FILE. A slot is claimed under
// streamsLock and freed by its close's last store (StreamWin.h).
#include <stdlib.h>
#include <errno.h>
#include "StreamWin.h"
#include "host/CioChannel.h"
#include "exit/Termination.h"

using rts6x::Stream;
using rts6x::CioChannel;

extern "C" FILE *__grt_streams[GRT_STREAMS];
FILE *__grt_streams[GRT_STREAMS];

namespace grt {
void *streamsLock;
}

extern "C" void __grt_streams_start(void)
{
    for (int i = 0; i < RTS6X_FTABLE_COUNT; i++) {
        _ftable[i].slot = i;
        __grt_streams[i] = &_ftable[i];
    }
    _ftable[2].flags |= grt::Unbuffered;
}

namespace grt {

static FILE *claim()
{
    int empty = -1;
    for (int i = RTS6X_FTABLE_COUNT; i < GRT_STREAMS; i++) {
        FILE *f = __grt_streams[i];
        if (f && f->flags == 0) {
            f->flags = Claimed;
            return f;
        }
        if (!f && empty < 0) empty = i;
    }
    if (empty >= 0) {
        FILE *f = (FILE *)calloc(1, sizeof(FILE));
        if (f) {
            f->slot = empty;
            f->flags = Claimed;
            __grt_streams[empty] = f;
            return f;
        }
    }
    errno = 24;     // EMFILE
    return 0;
}

FILE *freeStream()
{
    bool held = __grt_threads != 0;
    if (held) __grt_lock(&streamsLock);
    FILE *f = claim();
    if (held) __grt_unlock(&streamsLock);
    return f;
}

int closeStream(FILE *f, unsigned after)
{
    if (!(f->flags & Stream::Open)) {
        if (after) f->flags = after;
        return EOF;
    }
    int wrote = flush(f);
    int closed = f->fd > 2 ? __grt_close(f->fd) : 0;
    if (f->flags & Stream::OwnBuffer) free(f->buf);
    if (f->flags & Stream::Temporary) {
        char name[24];
        Stream::temporaryName(f->slot, name);
        __grt_unlink(name);
    }
    f->buf = f->pos = f->bufend = f->buff_stop = 0;
    f->flags = after;       // last: with 0, freeStream may hand the slot out from here on
    return wrote != 0 || closed < 0 ? EOF : 0;
}

// The standard streams are flushed, passed over without their lock when nothing waits (a thread may be blocked
// reading stdin, holding it); fopen's are closed under theirs, as the Microsoft runtime's _fcloseall closes them.
void closeAll()
{
    bool all = __grt_threads != 0;
    if (all) __grt_lock(&streamsLock);
    for (int i = 0; i < GRT_STREAMS; i++) {
        FILE *f = __grt_streams[i];
        if (!f || !(f->flags & Stream::Open)) continue;
        if (i < RTS6X_FTABLE_COUNT && !(f->flags & Pending)) continue;
        bool held = lock(f);
        if (i < RTS6X_FTABLE_COUNT) flush(f);
        else closeStream(f, 0);
        unlock(f, held);
    }
    if (all) __grt_unlock(&streamsLock);
}

}  // namespace grt

namespace rts6x {

bool Stream::parseMode(const char *mode, unsigned &host, unsigned &own)
{
    bool update = false, binary = false;
    if (!mode[0]) return false;
    for (const char *p = mode + 1; *p; p++) {
        if (*p == '+') update = true;
        else if (*p == 'b') binary = true;
        else if (*p == 't') binary = false;
        else return false;
    }
    switch (mode[0]) {
    case 'r': host = CioChannel::ReadOnly; own = Readable; break;
    case 'w': host = CioChannel::WriteOnly | CioChannel::Create | CioChannel::Truncate; own = Writable; break;
    case 'a': host = CioChannel::WriteOnly | CioChannel::Create | CioChannel::Append; own = Writable; break;
    default: return false;
    }
    if (update) {
        host = (host & ~3u) | CioChannel::ReadWrite;
        own = Readable | Writable;
    }
    if (binary) host |= CioChannel::Binary;
    return true;
}

// slot, when given, is Claimed (tmpfile's, freopen's); an open that fails frees it.
FILE *Stream::open(const char *path, const char *mode, FILE *slot)
{
    unsigned host, own;
    if (!parseMode(mode, host, own)) {
        if (slot) slot->flags = 0;
        errno = EINVAL;
        return 0;
    }
    if (!slot) slot = grt::freeStream();
    if (!slot) return 0;
    int fd = __grt_open(path, host);
    if (fd < 0) {
        slot->flags = 0;
        return 0;
    }
    slot->fd = fd;
    slot->buf = slot->pos = slot->bufend = slot->buff_stop = 0;
    slot->flags = Open | own | (host & CioChannel::Binary ? Binary : 0);
    Termination::closeStreamsWith(grt::closeAll);
    return slot;
}

int Stream::close()
{
    grt::valid(f_);
    return grt::closeStream(f_, 0);
}

void Stream::temporaryName(int slot, char *name)
{
    const char *stem = "grt-tmp";
    int n = 0;
    while (stem[n]) {
        name[n] = stem[n];
        n++;
    }
    name[n++] = (char)('0' + slot / 100 % 10);
    name[n++] = (char)('0' + slot / 10 % 10);
    name[n++] = (char)('0' + slot % 10);
    name[n] = 0;
}

}  // namespace rts6x
