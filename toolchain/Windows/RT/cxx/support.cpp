// Spec: the Microsoft C++ ABI's runtime entry points a cpp11 program calls outside RTTI and exceptions:
//   _purecall            the slot of a pure virtual function; reaching it ends the program by abort,
//                        silently, as the Microsoft runtime's Release build does
//   _Init_thread_header  a function-local static's guard (an int: 0 not made, -1 being made by some
//   _Init_thread_footer  thread, anything else made). cpp11 calls the header every time, and makes
//   _Init_thread_abort   the object when the guard then says -1; the footer marks it made, the abort
//                        (its constructor did not finish) makes it not made again. A second thread
//                        waits while the first makes it - ISO C++11 6.7/4.
//   __std_exception_copy, __std_exception_destroy   std::exception's message, as vcruntime keeps it
//                        (<exception>): copied when the object owns it, freed with the object.
// _purecall comes from RTS6x's __cxa_pure_virtual, the Itanium spelling of the same slot.

#include <stdlib.h>
#include <string.h>
#include "grt.h"

extern "C" int _purecall(void)
{
    abort();
    return 0;
}

namespace {

void *guardLock;       // an SRWLOCK, all zero to start
void *guardChanged;    // a CONDITION_VARIABLE, likewise

const int beingMade = -1;
const int made = 1;

}  // namespace

extern "C" void _Init_thread_header(int *guard)
{
    __grt_lock(&guardLock);
    if (*guard == 0) *guard = beingMade;
    else
        while (*guard == beingMade) {
            __grt_wait(&guardChanged, &guardLock);
            if (*guard == 0) { *guard = beingMade; break; }     // the other thread's constructor threw
        }
    __grt_unlock(&guardLock);
}

extern "C" void _Init_thread_footer(int *guard)
{
    __grt_lock(&guardLock);
    *guard = made;
    __grt_unlock(&guardLock);
    __grt_wake_all(&guardChanged);
}

extern "C" void _Init_thread_abort(int *guard)
{
    __grt_lock(&guardLock);
    *guard = 0;
    __grt_unlock(&guardLock);
    __grt_wake_all(&guardChanged);
}

struct __std_exception_data {
    const char *what;
    bool doFree;
};

extern "C" void __std_exception_copy(const __std_exception_data *from, __std_exception_data *to)
{
    if (from->doFree && from->what) {
        size_t n = strlen(from->what) + 1;
        char *copy = (char *)malloc(n);
        if (copy) {
            memcpy(copy, from->what, n);
            to->what = copy;
            to->doFree = true;
            return;
        }
    }
    to->what = from->what;
    to->doFree = false;
}

extern "C" void __std_exception_destroy(__std_exception_data *data)
{
    if (data->doFree) free((void *)data->what);
    data->what = 0;
    data->doFree = false;
}
