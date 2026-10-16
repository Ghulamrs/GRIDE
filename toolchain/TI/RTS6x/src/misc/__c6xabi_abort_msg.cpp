// Spec: SPRAB89B 8.2 and 9.2 - __c6xabi_abort_msg reports a failed assertion, then must not
// return: the message to stderr, then abort (ISO C 7.2.1.1).

#include "../stdio/OutputSink.h"
#include "../exit/Termination.h"

extern "C" void __c6xabi_abort_msg(const char *message)
{
    rts6x::OutputSink out(2);
    out.put(message);
    out.finish();
    rts6x::Termination::abort();
}
