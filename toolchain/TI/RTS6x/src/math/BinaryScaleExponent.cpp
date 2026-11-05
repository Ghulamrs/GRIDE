// Spec: ISO C99 7.12.6.5 ilogb and 7.12.6.11 logb: the exponent of a finite nonzero value, on the bits, so a subnormal
// has its own (the C674x's arithmetic reads one as zero).

#include "BinaryScale.h"
#include "MathBits.h"

namespace rts6x {

int BinaryScale::exponent(unsigned long long u)
{
    int e = MathBits::biased(u);
    if (e != 0) return e - 1023;
    unsigned long long f = u & MathBits::fractionMask();
    int top = 51;
    while (!(f >> top)) top--;
    return top - 1074;
}

}  // namespace rts6x
