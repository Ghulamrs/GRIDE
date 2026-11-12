/* getpid.c - Microsoft's _getpid (c90): Windows' number for this process (docs\Runtime_Design.md 9.4). */
#include <process.h>
#include "grt.h"
#include "win32.h"

int _getpid(void)
{
    return (int)GetCurrentProcessId();
}