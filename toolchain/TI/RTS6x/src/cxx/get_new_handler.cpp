// Spec: ISO C++11 18.6.2.4 - get_new_handler: the handler installed.

#include "Allocation.h"

std::new_handler std::get_new_handler() throw()
{
    return rts6x::Allocation::handler();
}
