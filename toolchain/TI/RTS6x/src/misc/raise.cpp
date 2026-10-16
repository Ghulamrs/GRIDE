// Spec: ISO C 7.14.2.1 - raise runs the handler for sig (SIG_DFL: the program ends abnormally,
// SIG_IGN: nothing); 0, or nonzero for a signal it does not know.

#include <signal.h>
#include "SignalTable.h"

extern "C" int raise(int sig)
{
    return rts6x::SignalTable::deliver(sig);
}
