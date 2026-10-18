// Spec: Microsoft C runtime, _setmode - the standard descriptors 0, 1 and 2 start open, in text mode;
// the C$$IO$$ host's open takes the descriptor from the target, the lowest free one, as measured.

#include "DescriptorModes.h"

namespace rts6x {

int DescriptorModes::modes_[DescriptorModes::Count] = { Text, Text, Text };

int DescriptorModes::vacant()
{
    for (int fd = 3; fd < Count; fd++)
        if (modes_[fd] == 0) return fd;
    return -1;
}

}  // namespace rts6x
