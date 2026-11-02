// Spec: ISO C++11 3.7.2 and 3.6.3/1 - a thread_local object is made in each thread that uses it and destroyed when
// that thread ends, the calling thread's before any static object at exit - in the Microsoft ABI cpp11 emits
// (docs\Runtime_Design.md 8.2):
//   __dyn_tls_init            the TLS callback (.CRT$XLC, cxx\tlsdata.asm): when a thread starts, every
//                             translation unit's initialiser in .CRT$XDA..XDZ, __tls_guard set first; once
//                             for each thread
//   __dyn_tls_on_demand_init  the same for a thread whose __tls_guard is still clear. The program's first
//                             thread, which Windows starts before the runtime is ready, has it called from
//                             .CRT$XCY, after the static objects' initialisers and before main, as the
//                             Microsoft runtime's start-up calls the callback
//   __tlregdtor               a thread_local object's destructor, registered by its initialiser
//   __grt_tls_end             the TLS callback (.CRT$XLD) that runs the ending thread's destructors, last
//                             registered first; exit runs the calling thread's (__grt_exit_thread)
#include <stdlib.h>
#include "grt.h"

typedef void (*Function)(void);

extern "C" Function __xd_a[], __xd_z[];

extern "C" thread_local bool __tls_guard = false;

namespace {

const unsigned long processDetach = 0, threadAttach = 2, threadDetach = 3;

thread_local Function *destructors = 0;
thread_local int destructorCount = 0;
thread_local int destructorRoom = 0;

void endThisThread()
{
    while (destructorCount > 0) destructors[--destructorCount]();
    free(destructors);
    destructors = 0;
    destructorRoom = 0;
}

}  // namespace

extern "C" void __dyn_tls_init(void *, unsigned long reason, void *)
{
    if (reason != threadAttach || __tls_guard) return;
    __tls_guard = true;
    for (Function *f = __xd_a; f < __xd_z; ++f)
        if (*f) (*f)();
}

extern "C" void __dyn_tls_on_demand_init()
{
    if (!__tls_guard) __dyn_tls_init(0, threadAttach, 0);
}

extern "C" int __tlregdtor(Function destructor)
{
    if (destructorCount == destructorRoom) {
        int room = destructorRoom ? destructorRoom * 2 : 16;
        Function *more = static_cast<Function *>(realloc(destructors, room * sizeof(Function)));
        if (!more) return -1;
        destructors = more;
        destructorRoom = room;
    }
    destructors[destructorCount++] = destructor;
    __grt_exit_thread = endThisThread;
    return 0;
}

extern "C" void __grt_tls_end(void *, unsigned long reason, void *)
{
    if (reason == threadDetach || reason == processDetach) endThisThread();
}
