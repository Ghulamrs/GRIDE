// Spec: Microsoft C runtime's old name getpid for _getpid (oldnames.lib), here for the C6747.

#define _POSIX_SOURCE 1
#include <process.h>

extern "C" int getpid(void)
{
    return _getpid();
}