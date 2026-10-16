// Spec: ISO C 7.14.1.1 and 7.14.2.1 - a handler per signal; SIG_DFL ends the program as abort does
// (status 134, as SIGABRT's default leaves it; abort itself delivers SIGABRT here first), SIG_IGN does
// nothing. A handler stays installed after it runs (7.14.1.1/3 lets the implementation choose; the hosts do).

#include <signal.h>
#include "SignalTable.h"
#include "../exit/Termination.h"

namespace rts6x {

SignalTable::Handler SignalTable::handlers_[SignalTable::Count];

SignalTable::Handler SignalTable::install(int sig, Handler handler)
{
    if (sig <= 0 || sig >= Count || handler == SIG_ERR) return SIG_ERR;
    Handler old = handlers_[sig];
    handlers_[sig] = handler;
    return old;
}

int SignalTable::deliver(int sig)
{
    if (sig <= 0 || sig >= Count) return -1;
    Handler h = handlers_[sig];
    if (h == SIG_IGN) return 0;
    if (h == SIG_DFL) Termination::abort();
    h(sig);
    return 0;
}

}  // namespace rts6x
