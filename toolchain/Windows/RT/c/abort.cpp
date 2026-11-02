// Spec: ISO C 7.20.4.1 - abort ends the program unsuccessfully, running no atexit function, after SIGABRT's handler
// if one is set (c\signals.cpp). As the Microsoft runtime ends it: a Release program by the fast fail, exit code
// 0xC0000409; a Debug one writes "abort() has been called" to stderr and ends with exit code 3, as the Debug runtime
// does once its dialog is answered - GRIDE's shows no dialog (docs\Runtime_Design.md 4.3).
#include <signal.h>
#include "misc/SignalTable.h"
#include "grt.h"

extern "C" void abort(void)
{
    rts6x::SignalTable::deliver(SIGABRT);
#if defined(GRT_DEBUG)
    static const char said[] = "abort() has been called\n";
    __grt_write(2, said, sizeof said - 1);
    __rts6x_halt(3);
#else
    __grt_fail_fast();
#endif
}
