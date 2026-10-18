// Spec: ISO C 7.12.7.2 fabs and F.9.4.2: |x|, the sign bit cleared, a NaN's payload kept.

#include "BinaryScale.h"
#include "MathBits.h"

namespace rts6x {

double BinaryScale::magnitude(double x) { return MathBits::absolute(x); }

}  // namespace rts6x
