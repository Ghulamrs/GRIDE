// Spec: ISO C 7.12.6.7 log and F.9.3.7: ln x, within an ulp.

#include "Logarithm.h"

namespace rts6x {

double Logarithm::natural(double x)
{
    // Positive, normal and finite on the high word alone: the kernel straight away.
    union { double d; unsigned w[2]; } v;
    v.d = x;
    double lo;
    // The kernel's result is held before lo is read: in `kernel(x, lo) + lo` the order of evaluation is
    // unspecified, and a compiler that reads lo first (cpp11 for x86-64 does) adds an unset lo.
    double hi;
    if (v.w[1] - 0x00100000u < 0x7FE00000u) { hi = kernel(x, lo); return hi + lo; }
    double result;
    if (special(x, result)) return result;
    hi = kernel(x, lo);
    return hi + lo;
}

}  // namespace rts6x
