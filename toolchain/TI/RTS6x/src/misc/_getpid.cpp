// Spec: Microsoft C runtime, _getpid - the process's number. The C6747 runs one program and no system, so 1.

#include <process.h>

extern "C" int _getpid(void)
{
    return 1;
}