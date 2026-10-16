// Spec: ISO C 7.20.4.3 - exit calls the registered functions, last first, and ends the program.

#include "Termination.h"

extern "C" void exit(int status)
{
    rts6x::Termination::exit(status);
}
