// Spec: Microsoft C runtime, _fstat64i32 (<sys/stat.h>'s _fstat and fstat) - an open descriptor's kind and size: the
// host's console _S_IFCHR, st_dev and st_rdev the descriptor; a file _S_IFREG, readable and writable by all three,
// its size the host's end of it (the position kept). The host tells no times: they are 0. 0, or -1 with errno
// EBADF and the struct zeroed.

#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _fstat64i32(int fd, struct _stat64i32 *buf)
{
    memset(buf, 0, sizeof *buf);
    if (rts6x::DescriptorModes::modeOf(fd) == 0) {
        ErrorNumber::set(EBADF);
        return -1;
    }
    buf->st_nlink = 1;
    if (rts6x::DescriptorModes::host(fd) <= 2) {
        buf->st_mode = _S_IFCHR;
        buf->st_dev = buf->st_rdev = static_cast<_dev_t>(fd);
        return 0;
    }
    long at = rts6x::CioChannel::seek(fd, 0, rts6x::CioChannel::FromHere);
    long end = rts6x::CioChannel::seek(fd, 0, rts6x::CioChannel::FromEnd);
    rts6x::CioChannel::seek(fd, at, rts6x::CioChannel::FromStart);
    buf->st_mode = _S_IFREG | 0x1B6;
    buf->st_size = end < 0 ? 0 : end;
    return 0;
}