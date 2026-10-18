// Spec: ISO C 7.12.6.7 log and F.9.3.7: ln x, within an ulp.

#include "Logarithm.h"

namespace rts6x {

double Logarithm::natural(double x)
{
    // Positive, normal and finite on the high word alone: the kernel straight away.
    union { double d; unsigned w[2]; } v;
    v.d = x;
    double lo;
    if (v.w[1] - 0x00100000u < 0x7FE00000u) return kernel(x, lo) + lo;
    double result;
    if (special(x, result)) return result;
    return kernel(x, lo) + lo;
}

}  // namespace rts6x
