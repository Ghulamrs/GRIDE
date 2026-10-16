// Spec: ISO C 7.12.9.2 and 7.12.9.4 - floor and ceil on the bits: below one, zero or -1 or +1;
// otherwise the fraction cleared, and carried into the integer when rounding away from zero.

#include "IntegralPart.h"
#include "MathBits.h"

namespace rts6x {

unsigned long long IntegralPart::directed(unsigned long long u, bool up)
{
    int e = MathBits::biased(u) - 1023;
    bool negative = MathBits::negative(u);
    if (e >= 52 || MathBits::isZero(u)) return u;
    // Rounding away from zero when the direction and the sign disagree.
    bool away = negative != up;
    if (e < 0) return away ? (u & MathBits::signBit()) | (1023ull << 52) : u & MathBits::signBit();
    unsigned long long mask = (1ull << (52 - e)) - 1;
    if ((u & mask) == 0) return u;
    return away ? (u + mask + 1) & ~mask : u & ~mask;
}

double IntegralPart::down(double x) { return MathBits::from(directed(MathBits::of(x), false)); }

double IntegralPart::up(double x) { return MathBits::from(directed(MathBits::of(x), true)); }

}  // namespace rts6x
