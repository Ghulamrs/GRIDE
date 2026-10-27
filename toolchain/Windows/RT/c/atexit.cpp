// Spec: ISO C 7.20.4.2 - atexit registers a function for exit; zero if it was registered. RTS6x's list
// (exit\Termination.cpp), changed under a lock once the program has threads (docs\Runtime_Design.md 9.6): cpp11
// registers a function-local static's destructor from whichever thread makes the static first, so two threads can
// register at once, and an unlocked count could then run a slot no one filled at exit.
#include "exit/Termination.h"
#include "grt.h"

namespace {
void *listLock;     // an SRWLOCK, all zero to start
}

extern "C" int atexit(void (*function)(void))
{
    bool held = __grt_threads != 0;
    if (held) __grt_lock(&listLock);
    int result = rts6x::Termination::add(function);
    if (held) __grt_unlock(&listLock);
    return result;
}
