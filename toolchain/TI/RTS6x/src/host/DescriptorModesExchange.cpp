// Spec: Microsoft C runtime, _setmode - EINVAL for a mode other than _O_TEXT or _O_BINARY, EBADF for a
// descriptor that is not open.
#include <errno.h>
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

namespace rts6x {

int DescriptorModes::exchange(int fd, int mode)
{
    if (fd < 0 || fd >= Count || modes_[fd] == 0) { ErrorNumber::set(EBADF); return -1; }
    if (mode != Text && mode != Binary) { ErrorNumber::set(EINVAL); return -1; }
    int old = modes_[fd];
    modes_[fd] = mode;
    return old;
}

}  // namespace rts6x
