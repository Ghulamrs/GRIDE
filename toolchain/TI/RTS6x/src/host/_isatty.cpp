// Spec: Microsoft C runtime, _isatty - non-zero for a descriptor on the host's console (0, 1 and 2 and what shares
// them), 0 for a file or one not open.
#include <errno.h>
#include <io.h>
#include "CioChannel.h"
#include "DescriptorModes.h"
#include "../misc/ErrorNumber.h"

using rts6x::ErrorNumber;
extern "C" int _isatty(int fd)
{
    return rts6x::DescriptorModes::modeOf(fd) != 0 && rts6x::DescriptorModes::host(fd) <= 2 ? 1 : 0;
}