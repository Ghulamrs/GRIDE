// Spec: SPRAB89B 18.3 - __TI_decompress_rle24, the handler lnk6x names in the handler table by that name.

#include "Decompressor.h"

extern "C" void __TI_decompress_rle24(const unsigned char *source, unsigned char *destination)
{
    rts6x::Decompressor::rle24(source, destination);
}
