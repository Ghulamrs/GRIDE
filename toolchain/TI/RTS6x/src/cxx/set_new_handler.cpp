// Spec: ISO C++11 18.6.2.3 - set_new_handler: installs the handler, answers the one before.

#include "Allocation.h"

std::new_handler std::set_new_handler(std::new_handler handler) throw()
{
    return rts6x::Allocation::exchange(handler);
}
