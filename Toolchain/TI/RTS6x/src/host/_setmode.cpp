// Spec: Microsoft C runtime, _setmode(fd, mode) - the previous translation mode, or -1 with errno set.

#include <io.h>
#include "DescriptorModes.h"

extern "C" int _setmode(int fd, int mode)
{
    return rts6x::DescriptorModes::exchange(fd, mode);
}
