// Spec: ISO C++11 18.8.3.4 - std::terminate.

#include "Handlers.h"

void std::terminate()
{
    rts6x::Handlers::terminate();
}
