// Spec: ISO C++11 18.8.4 - uncaught_exception: true between a throw and its handler's start.

#include "Handlers.h"
#include "Exception.h"

bool std::uncaught_exception() throw()
{
    return rts6x::Exception::uncaught() > 0;
}
