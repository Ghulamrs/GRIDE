// Spec: SPRAB89B 18.3 - __TI_decompress_none, the handler lnk6x names in the handler table by that name.

#include "Decompressor.h"

extern "C" void __TI_decompress_none(const unsigned char *source, unsigned char *destination)
{
    rts6x::Decompressor::none(source, destination);
}
