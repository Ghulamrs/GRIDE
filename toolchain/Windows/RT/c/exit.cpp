// Spec: ISO C 7.20.4.3 - exit calls the registered functions, last first, and ends the program; ISO C++11 3.6.3/1 -
// the calling thread's thread_local objects are destroyed before any object with static storage duration, which
// the Microsoft runtime's exit does first too. RTS6x's Termination does the rest (exit\exit.cpp's body).
// ISO C11 7.22.4.3 and 7.22.4.7 - at_quick_exit registers a function for quick_exit, which calls them, last first,
// and ends the program as _Exit does (C99 7.20.4.4): no atexit function, no static object's destructor, no stream
// written. As on the Microsoft runtime, the calling thread's thread_local objects are still destroyed then, by the
// TLS callback Windows calls as the process ends (cxx\threadlocal.cpp).
#include <stdlib.h>
#include "exit/Termination.h"
#include "grt.h"

void (*__grt_exit_thread)(void);

namespace {
void *quickLock;    // an SRWLOCK, all zero to start
void (**quickFunctions)(void);
int quickCount;
int quickRoom;
}

extern "C" void exit(int status)
{
    if (__grt_exit_thread) __grt_exit_thread();
    rts6x::Termination::exit(status);
}

extern "C" void _Exit(int status)
{
    __rts6x_halt(status);
}

extern "C" int at_quick_exit(void (*function)(void))
{
    bool held = __grt_threads != 0;
    if (held) __grt_lock(&quickLock);
    int result = 0;
    if (quickCount == quickRoom) {
        int room = quickRoom ? quickRoom * 2 : 32;
        void (**more)(void) = static_cast<void (**)(void)>(realloc(quickFunctions, room * sizeof *more));
        if (more) {
            quickFunctions = more;
            quickRoom = room;
        } else {
            result = -1;
        }
    }
    if (result == 0) quickFunctions[quickCount++] = function;
    if (held) __grt_unlock(&quickLock);
    return result;
}

extern "C" void quick_exit(int status)
{
    while (quickCount > 0) quickFunctions[--quickCount]();
    _Exit(status);
}
