// Spec: ISO C++11 18.8.3.3-4 (terminate calls the handler; a handler must not return, and if it
// does the program still ends) and D.11.4 (unexpected calls its handler, by default terminate).

#include "Handlers.h"
#include "../exit/Termination.h"

namespace rts6x {

std::terminate_handler Handlers::terminate_;
std::unexpected_handler Handlers::unexpected_;

std::terminate_handler Handlers::exchangeTerminate(std::terminate_handler next)
{
    std::terminate_handler old = terminate_;
    terminate_ = next;
    return old;
}

std::unexpected_handler Handlers::exchangeUnexpected(std::unexpected_handler next)
{
    std::unexpected_handler old = unexpected_;
    unexpected_ = next;
    return old;
}

void Handlers::terminate()
{
    std::terminate_handler h = terminate_;
    if (h) h();
    Termination::abort();
}

void Handlers::unexpected()
{
    std::unexpected_handler h = unexpected_;
    if (h) h();
    terminate();
}

}  // namespace rts6x
