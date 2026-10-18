// Spec: ISO C 7.19.5.3 (fopen: r, w, a, each with + and b in either order), 7.19.5.1 (fclose) and
// 7.19.4.3 (tmpfile: removed when closed). The host's flags say the same as the mode, binary only for
// a b (a text file on a Windows host has CR LF); errno EINVAL for a bad mode, ENOENT for a refusal.

#include <stdlib.h>
#include <errno.h>
#include "Stream.h"
#include "../host/CioChannel.h"
#include "../misc/ErrorNumber.h"
#include "../exit/Termination.h"

namespace rts6x {

namespace {

// Every stream still open when the program ends, closed (7.20.4.3/4): a tmpfile's is removed with it.
void closeAll()
{
    for (int i = Stream::FirstFree; i < RTS6X_FTABLE_COUNT; i++)
        if (_ftable[i].flags & Stream::Open) Stream(&_ftable[i]).close();
}

}  // namespace

bool Stream::parseMode(const char *mode, unsigned &host, unsigned &own)
{
    bool update = false, binary = false;
    if (!mode[0]) return false;
    for (const char *p = mode + 1; *p; p++) {
        if (*p == '+') update = true;
        else if (*p == 'b') binary = true;
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

FILE *Stream::open(const char *path, const char *mode, FILE *slot)
{
    unsigned host, own;
    if (!parseMode(mode, host, own)) {
        ErrorNumber::set(EINVAL);
        return 0;
    }
    for (int i = FirstFree; !slot && i < RTS6X_FTABLE_COUNT; i++)
        if (!(_ftable[i].flags & Open)) slot = &_ftable[i];
    if (!slot) return 0;
    // The host gives no reason for a refusal; the commonest is a file that is not there.
    int fd = CioChannel::open(path, host);
    if (fd < 0) {
        ErrorNumber::set(ENOENT);
        return 0;
    }
    slot->fd = fd;
    slot->buf = slot->pos = slot->bufend = slot->buff_stop = 0;
    slot->flags = Open | own | (host & CioChannel::Binary ? Binary : 0);
    Stream(slot).forgetFill();
    Termination::closeStreamsWith(closeAll);
    return slot;
}

int Stream::close()
{
    if (!(f_->flags & Open)) return EOF;
    int r = f_->fd > 2 ? CioChannel::close(f_->fd) : 0;
    if (f_->flags & OwnBuffer) free(f_->buf);
    if (f_->flags & Temporary) {
        char name[24];
        temporaryName((int)(f_ - _ftable), name);
        CioChannel::unlink(name);
    }
    f_->buf = f_->pos = f_->bufend = f_->buff_stop = 0;
    f_->flags = 0;
    return r < 0 ? EOF : 0;
}

void Stream::temporaryName(int slot, char *name)
{
    const char *stem = "rts6x-tmp";
    int n = 0;
    while (stem[n]) { name[n] = stem[n]; n++; }
    name[n++] = (char)('0' + slot / 10);
    name[n++] = (char)('0' + slot % 10);
    name[n] = 0;
}

}  // namespace rts6x
