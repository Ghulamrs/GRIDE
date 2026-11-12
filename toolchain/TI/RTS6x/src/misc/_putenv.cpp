// Spec: Microsoft C runtime, _putenv - "NAME=value" sets NAME for getenv, "NAME=" removes it; 0, or -1 with errno.
// The host's environment is not written: a program on the C6747 starts no other.

#include <errno.h>
#include <stdlib.h>
#include "Environment.h"
#include "ErrorNumber.h"

extern "C" int _putenv(const char *entry)
{
    if (entry == 0) {
        errno = EINVAL;
        return -1;
    }
    return rts6x::Environment::put(entry);
}