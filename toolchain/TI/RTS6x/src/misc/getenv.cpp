// Spec: ISO C 7.20.4.5 - getenv: the host's value for name, in a string a later call may overwrite;
// a null pointer if name has none.

#include <stdlib.h>
#include "../host/CioChannel.h"

extern "C" char *getenv(const char *name)
{
    static char value[256];
    return rts6x::CioChannel::environment(name, value, sizeof value) ? value : 0;
}
