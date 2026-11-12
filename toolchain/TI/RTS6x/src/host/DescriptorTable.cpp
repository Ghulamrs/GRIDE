// Spec: Microsoft C runtime, _setmode - the standard descriptors 0, 1 and 2 start open, in text mode;
// the C$$IO$$ host's open takes the descriptor from the target, the lowest free one, as measured.

#include "DescriptorModes.h"

namespace rts6x {

int DescriptorModes::modes_[DescriptorModes::Count] = { Text, Text, Text };
unsigned char DescriptorModes::alias_[DescriptorModes::Count];

// Not open, and no open descriptor's host descriptor either: the host still holds a number a _dup shares.
int DescriptorModes::vacant()
{
    for (int fd = 3; fd < Count; fd++) {
        if (modes_[fd] != 0) continue;
        bool held = false;
        for (int other = 0; other < Count && !held; other++) held = modes_[other] != 0 && host(other) == fd;
        if (!held) return fd;
    }
    return -1;
}

bool DescriptorModes::shared(int fd)
{
    for (int other = 0; other < Count; other++)
        if (other != fd && modes_[other] != 0 && host(other) == host(fd)) return true;
    return false;
}

}  // namespace rts6x
