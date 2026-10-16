// Spec: Itanium C++ ABI 3.3.2 - acquire answers 0 once the object is built, and 1 to the one caller
// that is to build it; ISO C++11 6.7/4 - re-entering the declaration while it is being built is
// undefined, and here it ends the program rather than build the object twice.

#include "Guard.h"

extern "C" void __c6xabi_abort_msg(const char *message);

namespace rts6x {

int Guard::acquire()
{
    if (bytes_[0]) return 0;
    if (bytes_[1]) __c6xabi_abort_msg("a static local's initialisation re-entered itself\n");
    bytes_[1] = 1;
    return 1;
}

}  // namespace rts6x
