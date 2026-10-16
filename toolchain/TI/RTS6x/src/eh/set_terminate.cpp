// Spec: ISO C++11 18.8.3.2 - set_terminate: the handler installed, the one before answered; null means the default.

#include "Handlers.h"

std::terminate_handler std::set_terminate(std::terminate_handler handler) throw()
{
    return rts6x::Handlers::exchangeTerminate(handler);
}
