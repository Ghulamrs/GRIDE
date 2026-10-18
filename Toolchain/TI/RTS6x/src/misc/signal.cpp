// Spec: ISO C 7.14.1.1 - signal: the handler for sig, the previous one answered; SIG_ERR for a
// signal it does not know. Nothing raises a signal on the C6747 but raise.

#include <signal.h>
#include "SignalTable.h"

extern "C" void (*signal(int sig, void (*handler)(int)))(int)
{
    return rts6x::SignalTable::install(sig, handler);
}
