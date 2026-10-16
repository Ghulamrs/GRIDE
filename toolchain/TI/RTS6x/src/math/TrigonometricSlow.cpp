// Spec: ISO C 7.12.4.5 cos, 7.12.4.6 sin for every finite x: ArgumentReduction and the
// double-double kernels, k mod 4 choosing among sin r, cos r, -sin r, -cos r (cos the sine shifted).

#include "Trigonometric.h"
#include "ArgumentReduction.h"

namespace rts6x {

double Trigonometric::slow(double x, int shift)
{
    DoubleDouble r;
    switch ((ArgumentReduction::reduce(x, r) + shift) & 3) {
    case 0: return sineKernel(r).value();
    case 1: return cosineKernel(r).value();
    case 2: return -sineKernel(r).value();
    default: return -cosineKernel(r).value();
    }
}

}  // namespace rts6x
