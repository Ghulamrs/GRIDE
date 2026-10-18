// Spec: ISO C++11 18.8.3.3 - get_terminate: the handler installed.

#include "Handlers.h"

std::terminate_handler std::get_terminate() throw()
{
    return rts6x::Handlers::terminating();
}
