// Spec: ISO C F.9.4.4: whether y is an odd integer, an even one, or not an integer - read from the
// fraction bits below the units place and the units bit itself. Every |y| >= 2^53 is even.

#include "Power.h"
#include "MathBits.h"

namespace rts6x {

Power::Parity Power::parity(unsigned long long y)
{
    int e = MathBits::biased(y) - 1023;
    if (e >= 53) return Even;
    if (e < 0) return NotInteger;
    unsigned long long significand = (y & MathBits::fractionMask()) | (1ull << 52);
    if (e < 52 && (significand & ((1ull << (52 - e)) - 1)) != 0) return NotInteger;
    return ((significand >> (52 - e)) & 1) ? Odd : Even;
}

}  // namespace rts6x
