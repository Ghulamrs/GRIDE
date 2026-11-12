// Spec: Microsoft C runtime, _open - a descriptor for path, the host's open given <fcntl.h>'s _O_ flags in its own
// numbering: _O_RDONLY, _O_WRONLY, _O_RDWR, _O_APPEND, _O_CREAT, _O_TRUNC, _O_BINARY (text otherwise, the host
// translating). _O_EXCL with _O_CREAT is a file that must not be there, asked of the host first; _O_TEMPORARY and
// the permission argument are not the host's to keep. -1 with errno EEXIST, or ENOENT for any refusal (the host
// gives no reason), or EMFILE when the 32 descriptors are open.

#include <fcntl.h>
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _open(const char *path, int oflag, ...)
{
    if (path == 0 || (oflag & 3) == 3) {
        ErrorNumber::set(EINVAL);
        return -1;
    }
    if (rts6x::DescriptorModes::vacant() < 0) {
        ErrorNumber::set(EMFILE);
        return -1;
    }
    unsigned flags = static_cast<unsigned>(oflag) & 3u;
    if (oflag & _O_APPEND) flags |= rts6x::CioChannel::Append;
    if (oflag & _O_CREAT) flags |= rts6x::CioChannel::Create;
    if (oflag & _O_TRUNC) flags |= rts6x::CioChannel::Truncate;
    if (oflag & _O_BINARY) flags |= rts6x::CioChannel::Binary;
    if ((oflag & (_O_CREAT | _O_EXCL)) == (_O_CREAT | _O_EXCL)) {
        int there = rts6x::CioChannel::open(path, rts6x::CioChannel::ReadOnly);
        if (there >= 0) {
            rts6x::CioChannel::close(there);
            ErrorNumber::set(EEXIST);
            return -1;
        }
    }
    int fd = rts6x::CioChannel::open(path, flags);
    if (fd < 0) ErrorNumber::set(ENOENT);
    return fd;
}