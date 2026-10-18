// Spec: ISO C 7.20.4.2 - atexit registers a function for exit; zero if it was registered.

#include "Termination.h"

extern "C" int atexit(void (*function)(void))
{
    return rts6x::Termination::add(function);
}
