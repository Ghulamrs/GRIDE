// Spec: Itanium C++ ABI EH 2.5.3 and ARM EHABI 8.4 - __cxa_call_unexpected: an exception left a
// function whose specification does not allow it; unexpected(), whose default is terminate.

#include "Handlers.h"

extern "C" void __cxa_call_unexpected(void *exception)
{
    (void)exception;
    rts6x::Handlers::unexpected();
}
