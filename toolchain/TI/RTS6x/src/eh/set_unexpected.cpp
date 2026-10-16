// Spec: ISO C++11 D.11.2 - set_unexpected.

#include "Handlers.h"

std::unexpected_handler std::set_unexpected(std::unexpected_handler handler) throw()
{
    return rts6x::Handlers::exchangeUnexpected(handler);
}
