// Spec: SPRAB89B 18.3 (uncompressed and zero-initialised cinit data) and the rle24 stream as
// LNK6x's rle24_encode writes it - the encoder of this toolchain, so the two are one format.

#include "Decompressor.h"

namespace rts6x {

void Decompressor::none(const unsigned char *source, unsigned char *destination)
{
    unsigned n = size(source);
    const unsigned char *from = source + 7;
    for (unsigned i = 0; i < n; i++) destination[i] = from[i];
}

void Decompressor::zero(const unsigned char *source, unsigned char *destination)
{
    fill(destination, 0, size(source));
}

// After the escape byte, literals; the escape and a count: 1-3 that many escapes, 4-255 a run of
// the next byte, 0 a 16-bit big-endian length (below 256, the top of a 24-bit one), a 0 length the end.
void Decompressor::rle24(const unsigned char *source, unsigned char *destination)
{
    const unsigned char escape = *source++;
    for (;;) {
        unsigned char b = *source++;
        if (b != escape) { *destination++ = b; continue; }
        unsigned count = *source++;
        if (count >= 4) { fill(destination, *source++, count); continue; }
        if (count != 0) { fill(destination, escape, count); continue; }
        unsigned length = (source[0] << 8) | source[1];
        source += 2;
        if (length == 0) return;
        if (length < 256) {
            length = (length << 16) | (source[0] << 8) | source[1];
            source += 2;
        }
        fill(destination, *source++, length);
    }
}

}  // namespace rts6x
