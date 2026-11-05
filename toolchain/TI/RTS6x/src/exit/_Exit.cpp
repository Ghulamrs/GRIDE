// Spec: ISO C99 7.20.4.4 - _Exit ends the program at once: no atexit function, no stream written.

#include <stdlib.h>
#include "Termination.h"

extern "C" void _Exit(int status)
{
    __rts6x_halt(status);
}
