// Spec: ISO C99 7.12.8.3 and 7.12.8.4 - Stirling's series and the argument raised to it (GammaFunction.h).

#include "GammaFunction.h"
#include "ExpKernel.h"
#include "ExtendedConstants.h"
#include "Logarithm.h"

namespace {

// Stirling's B(2j) / (2j (2j - 1)), j = 1 .. 10: j = 1 as a double-double
const unsigned long long series[10] = {
    0x3FB5555555555555ULL, 0xBF66C16C16C16C17ULL, 0x3F4A01A01A01A01AULL, 0xBF43813813813814ULL, 0x3F4B951E2B18FF23ULL,
    0xBF5F6AB0D9993C7DULL, 0x3F7A41A41A41A41AULL, 0xBF9E4286CB0F5398ULL, 0x3FC6FE96381E0680ULL, 0xBFF6476701181F3AULL };
const unsigned long long seriesOneLow = 0x3C55555555555555ULL;

}  // namespace

namespace rts6x {

DoubleDouble GammaFunction::stirling(const DoubleDouble &z)
{
    DoubleDouble r = z.plus(-0.5).times(Logarithm::extended(z)).minus(z).plus(ExtendedConstants::halfLnTwoPi());
    DoubleDouble first = ExtendedConstants::of(series[0], seriesOneLow).over(z);
    double z2 = z.hi * z.hi, power = z.hi, tail = 0.0;
    for (int j = 1; j < 10; j++) {
        power *= z2;
        tail += MathBits::from(series[j]) / power;
    }
    return r.plus(first.plus(tail));
}

DoubleDouble GammaFunction::gamma(const DoubleDouble &y, int &k)
{
    DoubleDouble z = y, product(1.0, 0.0);
    while (z.hi < 20.0) {
        product = product.times(z);
        z = z.plus(1.0);
    }
    return ExpKernel::extended(stirling(z), k).over(product);
}

DoubleDouble GammaFunction::logGamma(const DoubleDouble &y)
{
    if (y.hi >= 20.0) return stirling(y);
    int k;
    DoubleDouble m = gamma(y, k);
    return Logarithm::extended(m).plus(ExtendedConstants::lnTwo().times((double)k));
}

}  // namespace rts6x
