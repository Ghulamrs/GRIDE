// Spec: ISO C99 7.12.6.2 exp2, 7.12.6.3 expm1 and 7.12.8: e^z as m 2^k over the kernel's range and past it, where
// Gamma's logarithm goes before its division brings the value back.

#include "ExpKernel.h"
#include "ExtendedConstants.h"

namespace rts6x {

DoubleDouble ExpKernel::extended(DoubleDouble z, int &k)
{
    int extra = 0;
    if (z.hi > 700.0) {
        z = z.minus(ExtendedConstants::lnTwo().times(512.0));
        extra = 512;
    }
    DoubleDouble m;
    k = evaluate(z, m) + extra;
    return m;
}

}  // namespace rts6x
