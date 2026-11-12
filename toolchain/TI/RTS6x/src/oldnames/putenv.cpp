// Spec: Microsoft C runtime's old name putenv for _putenv (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <stdlib.h>

extern "C" int putenv(const char *entry)
{
    return _putenv(entry);
}