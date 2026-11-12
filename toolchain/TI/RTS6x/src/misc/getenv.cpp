// Spec: ISO C 7.20.4.5 - getenv: the host's value for name, in a string a later call may overwrite;
// a null pointer if name has none.

#include <stdlib.h>
#include "../host/CioChannel.h"
#include "Environment.h"

extern "C" char *getenv(const char *name)
{
    // what the program put (_putenv) first, then the host's
    const char *own = 0;
    int said = rts6x::Environment::lookup(name, &own);
    if (said != 0) return said > 0 ? const_cast<char *>(own) : 0;
    static char value[256];
    return rts6x::CioChannel::environment(name, value, sizeof value) ? value : 0;
}
