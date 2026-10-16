// Spec: ISO C 7.12.6.7, 7.12.6.8 and F.9.3.7, F.9.3.8: log(+-0) is -inf, a pole error (ERANGE);
// log(1) is +0; below zero a domain error; log(+inf) is +inf; a NaN returns itself.

#include "Logarithm.h"
#include "MathBits.h"
#include "MathError.h"

namespace rts6x {

bool Logarithm::special(double x, double &result)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) { result = x; return true; }
    if (MathBits::isZero(u)) { result = MathError::overflow(true); return true; }
    if (MathBits::negative(u)) { result = MathError::domain(); return true; }
    if (MathBits::isInfinite(u)) { result = x; return true; }
    return false;
}

}  // namespace rts6x
