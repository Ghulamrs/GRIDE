// Spec: SPRAB89B 18.2-18.3 (a handler gets the bytes after the index and the destination; the
// uncompressed and zero formats: 3 bytes of padding, then a 4-byte size) and LNK6x's rle24_encode
// (src/link.cpp), which states the rle24 stream it writes. One class reads all three formats.
#ifndef RTS6X_DECOMPRESSOR_H
#define RTS6X_DECOMPRESSOR_H

namespace rts6x {

class Decompressor {
public:
    // size bytes copied from after the size field.
    static void none(const unsigned char *source, unsigned char *destination);
    // size bytes of zero.
    static void zero(const unsigned char *source, unsigned char *destination);
    // The rle24 stream LNK6x's rle24_encode writes: an escape, literals, escape-led counts.
    static void rle24(const unsigned char *source, unsigned char *destination);

private:
    // The size field: after the index byte and 3 bytes of padding, little-endian.
    static unsigned size(const unsigned char *source)
    {
        const unsigned char *p = source + 3;
        return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
    }
    static void fill(unsigned char *&destination, unsigned char value, unsigned count)
    {
        for (unsigned i = 0; i < count; i++) *destination++ = value;
    }
};

}  // namespace rts6x

#endif
