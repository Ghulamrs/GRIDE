// Spec: positional notation, base 8, 10 or 16. Ten does not divide a power of two: a decimal digit of
// a 64-bit magnitude comes from long division over its four 16-bit pieces until the high word is 0,
// the rest two digits a turn from a 32-bit word divided by 100; 8 and 16 are shifts of the pair.

#include "IntegerDigits.h"

namespace rts6x {

const char IntegerDigits::pairs_[201] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

IntegerDigits::IntegerDigits(unsigned hi, unsigned lo, unsigned base, bool upper) : length_(0)
{
    char *end = text_ + sizeof text_;
    if (base == 10) {
        while (hi != 0) {
            unsigned piece[4] = { hi >> 16, hi & 0xFFFFu, lo >> 16, lo & 0xFFFFu };
            unsigned r = 0;
            for (int k = 0; k < 4; k++) {
                unsigned cur = r * 65536u + piece[k];
                piece[k] = cur / 10u;
                r = cur - piece[k] * 10u;
            }
            hi = (piece[0] << 16) | piece[1];
            lo = (piece[2] << 16) | piece[3];
            *--end = (char)('0' + r);
        }
        while (lo >= 100) {
            unsigned q = lo / 100u, r = 2 * (lo - q * 100u);
            *--end = pairs_[r + 1];
            *--end = pairs_[r];
            lo = q;
        }
        if (lo >= 10) { *--end = pairs_[2 * lo + 1]; *--end = pairs_[2 * lo]; }
        else if (lo != 0) *--end = (char)('0' + lo);
    } else {
        const char *glyphs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
        unsigned bits = base == 16 ? 4 : 3, mask = base - 1;
        while (hi != 0) {
            *--end = glyphs[lo & mask];
            lo = (lo >> bits) | (hi << (32 - bits));
            hi >>= bits;
        }
        for (; lo != 0; lo >>= bits) *--end = glyphs[lo & mask];
    }
    length_ = (unsigned)(text_ + sizeof text_ - end);
}

}  // namespace rts6x
