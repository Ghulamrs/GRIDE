// Spec: Itanium C++ ABI EH 2.5.3 - __cxa_end_catch: the latest handler done.

#include "Exception.h"

extern "C" void __cxa_end_catch(void)
{
    rts6x::Exception::end();
}
