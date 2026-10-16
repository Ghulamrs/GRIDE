// Spec: SPRAB89B 18.3 - __TI_zero_init, the handler lnk6x names in the handler table by that name.

#include "Decompressor.h"

extern "C" void __TI_zero_init(const unsigned char *source, unsigned char *destination)
{
    rts6x::Decompressor::zero(source, destination);
}
