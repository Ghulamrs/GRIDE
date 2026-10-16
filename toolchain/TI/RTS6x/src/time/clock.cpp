// Spec: ISO C 7.23.2.1 - clock: processor time used, in CLOCKS_PER_SEC (a million) units; the host
// counts the C6747's cycles, 300 to the microsecond at its 300 MHz.

#include <time.h>
#include "../host/CioChannel.h"

extern "C" clock_t clock(void)
{
    return (clock_t)(rts6x::CioChannel::cycles() / 300);
}
