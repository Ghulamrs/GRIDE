// Spec: ISO C++11 D.11.3 - get_unexpected.

#include "Handlers.h"

std::unexpected_handler std::get_unexpected() throw()
{
    return rts6x::Handlers::unexpecting();
}
