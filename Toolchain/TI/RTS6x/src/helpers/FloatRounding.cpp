// Spec: SPRAB89B 8.5 Table 8-12 and ISO C99 7.12.9.6 and 7.12.9.8: trunc, the integer toward zero,
// and round, the nearest integer with halfway cases away from zero - binary64 to binary64, exact,
// by clearing (or carrying into) the fraction bits below the units place.

#include "SoftFloat.h"

namespace rts6x {

unsigned long long FloatArithmetic::truncate(unsigned long long bits)
{
    int e = (int)((bits >> 52) & 0x7FF) - 1023;
    if (e >= 52) return bits;                           // integral already, or infinity or NaN
    if (e < 0) return bits & (1ull << 63);              // a signed zero
    return bits & ~((1ull << (52 - e)) - 1);
}

unsigned long long FloatArithmetic::roundHalfAway(unsigned long long bits)
{
    int e = (int)((bits >> 52) & 0x7FF) - 1023;
    unsigned long long sign = bits & (1ull << 63);
    if (e >= 52) return bits;
    if (e < -1) return sign;                            // below one half: a signed zero
    if (e == -1) return sign | (1023ull << 52);         // one half up to one: one
    unsigned long long half = 1ull << (51 - e), mask = (half << 1) - 1;
    return (bits + half) & ~mask;                       // a carry into the exponent is IEEE's own rounding
}

}  // namespace rts6x
