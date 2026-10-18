// Spec: ISO C 7.20.4.1 - abort ends the program unsuccessfully, calling no atexit function.

#include "Termination.h"

extern "C" void abort(void)
{
    rts6x::Termination::abort();
}
