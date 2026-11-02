// Spec: ISO C99 7.12.6.2 exp2, 7.12.6.3 expm1, 7.12.6.9 log1p, 7.12.5.2 asinh, 7.12.5.1 acosh, 7.12.5.3 atanh, 7.12.8.1
// erf, 7.12.8.2 erfc, 7.12.8.4 tgamma and 7.12.8.3 lgamma, with F.9's special values and the errno the Microsoft
// runtime's UCRT sets (tests\math99): a domain error is the quiet NaN 0x7FF8000000000000 and EDOM; a pole or an
// overflow HUGE_VAL and ERANGE; an underflow to zero ERANGE for erfc and tgamma (tgamma's +0 whatever the sign) and
// nothing for exp2. Each is computed in double-double arithmetic - RTS6x's DoubleDouble, e^z from ExpKernel and ln x
// from Logarithm::kernel, both to about 2^-67 - and rounded once at the end: within an ulp, nearly always half.
//   erf, erfc  below 2, (2/sqrt(pi)) a e^(-a^2) sum (2a^2)^n / (1 3 5 ... (2n+1)), whose terms are all positive;
//              from 2, erfc by Laplace's continued fraction e^(-a^2) / (sqrt(pi) (a + (1/2)/(a + 1/(a + ...))))
//   tgamma     Stirling's series from 20 up, the argument raised to 20 by Gamma(y) = Gamma(y + n) / (y ... (y+n-1)),
//              and below 1/2 the reflection pi / (sin(pi x) Gamma(1 - x))
//   lgamma     the same, and within 0.2 of 1 and of 2, where it crosses zero, its Taylor series there:
//              lgamma(1 + e) = -gamma e + sum (-1)^k zeta(k)/k e^k, lgamma(2 + e) = (1 - gamma) e +
//              sum (-1)^k (zeta(k) - 1)/k e^k
// The constants are double-doubles worked out to 90 digits.
#include <errno.h>
#include <math.h>
#include "math/MathBits.h"
#include "math/DoubleDouble.h"
#include "math/ExpKernel.h"
#include "math/Logarithm.h"
#include "math/BinaryScale.h"

using rts6x::BinaryScale;
using rts6x::DoubleDouble;
using rts6x::ExpKernel;
using rts6x::Logarithm;
using rts6x::MathBits;

namespace {

DoubleDouble constant(unsigned long long hi, unsigned long long lo)
{
    return DoubleDouble(MathBits::from(hi), MathBits::from(lo));
}

DoubleDouble pi() { return constant(0x400921FB54442D18ULL, 0x3CA1A62633145C07ULL); }
DoubleDouble lnTwo() { return constant(0x3FE62E42FEFA39EFULL, 0x3C7ABC9E3B39803FULL); }
DoubleDouble lnPi() { return constant(0x3FF250D048E7A1BDULL, 0x3C67ABF2AD8D5088ULL); }
DoubleDouble twoOverRootPi() { return constant(0x3FF20DD750429B6DULL, 0x3C71AE3A914FED80ULL); }
DoubleDouble oneOverRootPi() { return constant(0x3FE20DD750429B6DULL, 0x3C61AE3A914FED80ULL); }
DoubleDouble halfLnTwoPi() { return constant(0x3FED67F1C864BEB5ULL, 0xBC865B5A1B7FF5DFULL); }
DoubleDouble eulerGamma() { return constant(0x3FE2788CFC6FB619ULL, 0xBC56CB90701FBFABULL); }
DoubleDouble oneLessGamma() { return constant(0x3FDB0EE6072093CEULL, 0x3C56CB90701FBFABULL); }

// zeta(k)/k and (zeta(k) - 1)/k, k = 2 .. 29: k = 2 as a double-double, the rest their high parts
const unsigned long long zetaOverK[28] = {
    0x3FEA51A6625307D3ULL, 0x3FD9A4D55BEAB2D7ULL, 0x3FD151322AC7D848ULL, 0x3FCA8B9C17AA6149ULL, 0x3FC5B40CB100C306ULL,
    0x3FC2703A1DCEA3AEULL, 0x3FC010B36AF86397ULL, 0x3FBC806706D57DB4ULL, 0x3FB9A01E385D5F8FULL, 0x3FB748C33114C6D6ULL,
    0x3FB556AD63243BC4ULL, 0x3FB3B1D971FC5985ULL, 0x3FB2496DF8320C5FULL, 0x3FB11133476E7FE0ULL, 0x3FB00010064CDEB2ULL,
    0x3FAE1E2D311E8ABDULL, 0x3FAC71CE3A20B419ULL, 0x3FAAF28A1B5688A0ULL, 0x3FA9999B3352D5BAULL, 0x3FA86186DB77BFBFULL,
    0x3FA745D1D1778DF9ULL, 0x3FA642C88591B66DULL, 0x3FA555556AAAFDCDULL, 0x3FA47AE151EB9FB7ULL, 0x3FA3B13B189D925EULL,
    0x3FA2F684C00002BCULL, 0x3FA24924936DB7BCULL, 0x3FA1A7B961A7B9AAULL };
const unsigned long long zetaLessOneOverK[28] = {
    0x3FD4A34CC4A60FA6ULL, 0x3FB13E001A557607ULL, 0x3F951322AC7D8483ULL, 0x3F7E404FC218F5F2ULL, 0x3F67ADD6EADB6C30ULL,
    0x3F538AC5C2BF8E08ULL, 0x3F40B36AF86396E9ULL, 0x3F2D3FD4C76D2FC8ULL, 0x3F1A127B0F17D65AULL, 0x3F078DE5BD7C81EFULL,
    0x3EF580DCEE66EB02ULL, 0x3EE3CBC963CE2243ULL, 0x3ED2597A39F34AACULL, 0x3EC11B2EB7679541ULL, 0x3EB0064CDEB22F0FULL,
    0x3E9E2600D93CFD2FULL, 0x3E8C76BBB3F07A4DULL, 0x3E7AF5A6CBBF8A97ULL, 0x3E699B93C2070B0FULL, 0x3E5862C734DF3EACULL,
    0x3E47469DACCFADCDULL, 0x3E36434A8447AEADULL, 0x3E2555A877FFD2C3ULL, 0x3E147B1679258D0EULL, 0x3E03B15D2B2FC10CULL,
    0x3DF2F69A9FABE3E0ULL, 0x3DE24932A337434CULL, 0x3DD1A7C26EC2523CULL };
const unsigned long long zetaTwoLow = 0x3C71873D8912200CULL;   // the low part of zeta(2)/2 and (zeta(2) - 1)/2

// Stirling's B(2j) / (2j (2j - 1)), j = 1 .. 10: j = 1 as a double-double
const unsigned long long stirling[10] = {
    0x3FB5555555555555ULL, 0xBF66C16C16C16C17ULL, 0x3F4A01A01A01A01AULL, 0xBF43813813813814ULL, 0x3F4B951E2B18FF23ULL,
    0xBF5F6AB0D9993C7DULL, 0x3F7A41A41A41A41AULL, 0xBF9E4286CB0F5398ULL, 0x3FC6FE96381E0680ULL, 0xBFF6476701181F3AULL };
const unsigned long long stirlingOneLow = 0x3C55555555555555ULL;

double quietNaN()
{
    return MathBits::from(0x7FF8000000000000ull);
}

double domain()
{
    errno = EDOM;
    return quietNaN();
}

double overflow(bool negative)
{
    errno = ERANGE;
    return MathBits::infinity(negative);
}

// t / d for a double d, to about 2^-104
DoubleDouble dividedBy(const DoubleDouble &t, double d)
{
    double q = t.hi / d;
    DoubleDouble p = DoubleDouble::product(q, d);
    double r = ((t.hi - p.hi) - p.lo + t.lo) / d;
    return DoubleDouble::quickSum(q, r);
}

// ln a for a double-double a > 0: ln a.hi from the kernel, and ln(1 + a.lo / a.hi) by its series' first terms
DoubleDouble logOf(const DoubleDouble &a)
{
    double lo;
    double hi = Logarithm::kernel(a.hi, lo);
    double t = a.lo / a.hi;
    return DoubleDouble::quickSum(hi, lo + (t - 0.5 * t * t));
}

// e^z = m 2^k, |z.hi| <= 746 + 512 ln 2
DoubleDouble expOf(DoubleDouble z, int &k)
{
    int extra = 0;
    if (z.hi > 700.0) {
        z = z.minus(lnTwo().times(512.0));
        extra = 512;
    }
    DoubleDouble m;
    k = ExpKernel::evaluate(z, m) + extra;
    return m;
}

// sqrt(v), v > 0: the double root corrected once by its exact residual
DoubleDouble rootOf(const DoubleDouble &v)
{
    double s = sqrt(v.hi);
    DoubleDouble square = DoubleDouble::product(s, s);
    double correction = (((v.hi - square.hi) - square.lo) + v.lo) / (2.0 * s);
    return DoubleDouble::quickSum(s, correction);
}

// erf(a), 0 <= a < 6, by the series of positive terms
DoubleDouble erfSeries(double a)
{
    DoubleDouble a2 = DoubleDouble::product(a, a);
    DoubleDouble twoA2(2.0 * a2.hi, 2.0 * a2.lo);
    DoubleDouble term(1.0, 0.0), sum(1.0, 0.0);
    for (int n = 1; n < 400; n++) {
        term = dividedBy(term.times(twoA2), 2.0 * n + 1.0);
        sum = sum.plus(term);
        if (term.hi < sum.hi * 1e-34) break;
    }
    int k;
    DoubleDouble e = expOf(a2.negated(), k);
    e = ExpKernel::scaled(e, k);
    return twoOverRootPi().times(e).times(sum).times(a);
}

// erfc(a) = m 2^k, 2 <= a <= 27.3, by the continued fraction
DoubleDouble erfcFraction(double a, int &k)
{
    int depth = 40 + (int)(300.0 / (a * a));
    DoubleDouble t(a, 0.0);
    for (int n = depth; n >= 1; n--) t = DoubleDouble(0.5 * n, 0.0).over(t).plus(a);
    DoubleDouble e = expOf(DoubleDouble::product(a, a).negated(), k);
    return e.times(oneOverRootPi()).over(t);
}

// sin(pi x) for x not an integer, |x| < 2^52: x less an even integer, exactly, then sin(pi r) for |r| <= 1/2
DoubleDouble sinPi(double x)
{
    double r = x - 2.0 * rint(0.5 * x);
    bool negative = r < 0;
    if (negative) r = -r;
    if (r > 0.5) r = 1.0 - r;
    DoubleDouble t = pi().times(r), t2 = t.times(t), term = t, sum = t;
    for (int n = 1; n < 30; n++) {
        term = dividedBy(term.times(t2), -(2.0 * n) * (2.0 * n + 1.0));
        sum = sum.plus(term);
        if (fabs(term.hi) <= fabs(sum.hi) * 1e-34) break;
    }
    return negative ? sum.negated() : sum;
}

// ln Gamma(z), z >= 20: Stirling's series
DoubleDouble lnGammaStirling(const DoubleDouble &z)
{
    DoubleDouble r = z.plus(-0.5).times(logOf(z)).minus(z).plus(halfLnTwoPi());
    DoubleDouble first = constant(stirling[0], stirlingOneLow).over(z);
    double z2 = z.hi * z.hi, power = z.hi, tail = 0.0;
    for (int j = 1; j < 10; j++) {
        power *= z2;
        tail += MathBits::from(stirling[j]) / power;
    }
    return r.plus(first.plus(tail));
}

// Gamma(y) = m 2^k for y >= 1/2: y raised to 20 or more, Stirling's series there
DoubleDouble gammaOf(const DoubleDouble &y, int &k)
{
    DoubleDouble z = y, product(1.0, 0.0);
    while (z.hi < 20.0) {
        product = product.times(z);
        z = z.plus(1.0);
    }
    return expOf(lnGammaStirling(z), k).over(product);
}

// ln Gamma(y) for y >= 1/2 away from 1 and 2
DoubleDouble lnGammaOf(const DoubleDouble &y)
{
    if (y.hi >= 20.0) return lnGammaStirling(y);
    int k;
    DoubleDouble m = gammaOf(y, k);
    return logOf(m).plus(lnTwo().times((double)k));
}

// lgamma(1 + e) or lgamma(2 + e), |e| < 0.2, by the Taylor series whose first coefficient is c1 and whose others
// are table's
double nearRoot(double e, const DoubleDouble &c1, const unsigned long long *table)
{
    double tail = 0.0;
    for (int k = 29; k >= 3; k--) {
        double c = MathBits::from(table[k - 2]);
        tail = tail * e + ((k & 1) ? -c : c);
    }
    DoubleDouble second = constant(table[0], zetaTwoLow);
    DoubleDouble r = c1.times(e).plus(second.times(e).times(e));
    return r.plus(tail * e * e * e).value();
}

}  // namespace

extern "C" double exp2(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? x : 0.0;
    if (x >= 1024.0) return overflow(false);
    if (x < -1080.0) return 0.0;
    double n = rint(x), f = x - n;
    DoubleDouble z = DoubleDouble::product(f, lnTwo().hi).plus(f * lnTwo().lo);
    int k;
    DoubleDouble m = expOf(z, k);
    int saved = errno;
    double r = BinaryScale::scale(m.value(), k + (int)n);
    if (r == 0.0) errno = saved;
    return r;
}

extern "C" double expm1(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? x : -1.0;
    if (x > 709.8) return overflow(false);
    if (x < -38.0) return -1.0;
    double a = fabs(x);
    if (a < 5.551115123125783e-17) return x;
    if (a < 0.35) {
        DoubleDouble term(x, 0.0), sum(x, 0.0);
        for (int n = 2; n < 40; n++) {
            term = dividedBy(term.times(x), (double)n);
            sum = sum.plus(term);
            if (fabs(term.hi) < fabs(sum.hi) * 1e-34) break;
        }
        return sum.value();
    }
    int k;
    DoubleDouble m = expOf(DoubleDouble(x, 0.0), k);
    if (k > 950) return BinaryScale::scale(m.value(), k);
    return ExpKernel::scaled(m, k).plus(-1.0).value();
}

extern "C" double log1p(double x)
{
    if (x != x) return x + x;
    if (x == -1.0) return overflow(true);
    if (x < -1.0) return domain();
    if (MathBits::isInfinite(MathBits::of(x))) return x;
    if (fabs(x) < 5.551115123125783e-17) return x;
    return logOf(DoubleDouble::sum(1.0, x)).value();
}

extern "C" double asinh(double x)
{
    if (!MathBits::isFinite(MathBits::of(x))) return x + x;
    double a = fabs(x);
    if (a < 3.725290298461914e-09) return x;
    DoubleDouble r;
    if (a > 268435456.0) r = logOf(DoubleDouble(a, 0.0)).plus(lnTwo());
    else r = logOf(rootOf(DoubleDouble::product(a, a).plus(1.0)).plus(a));
    return x < 0 ? -r.value() : r.value();
}

extern "C" double acosh(double x)
{
    if (x != x) return x + x;
    if (x < 1.0) return domain();
    if (MathBits::isInfinite(MathBits::of(x))) return x;
    if (x == 1.0) return 0.0;
    if (x > 268435456.0) return logOf(DoubleDouble(x, 0.0)).plus(lnTwo()).value();
    return logOf(rootOf(DoubleDouble::product(x, x).plus(-1.0)).plus(x)).value();
}

extern "C" double atanh(double x)
{
    if (x != x) return x + x;
    double a = fabs(x);
    if (a > 1.0) return domain();
    if (a == 1.0) return overflow(x < 0);
    if (a < 3.725290298461914e-09) return x;
    DoubleDouble q = DoubleDouble::sum(1.0, a).over(DoubleDouble::sum(1.0, -a));
    double r = 0.5 * logOf(q).value();
    return x < 0 ? -r : r;
}

extern "C" double erf(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? 1.0 : -1.0;
    double a = fabs(x);
    if (a < 3.725290298461914e-09) return x * twoOverRootPi().hi;
    if (a >= 6.0) return x > 0 ? 1.0 : -1.0;
    double r;
    if (a < 2.0) {
        r = erfSeries(a).value();
    } else {
        int k;
        DoubleDouble c = erfcFraction(a, k);
        r = DoubleDouble(1.0, 0.0).minus(ExpKernel::scaled(c, k)).value();
    }
    return x < 0 ? -r : r;
}

extern "C" double erfc(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? 0.0 : 2.0;
    if (x < -6.0) return 2.0;
    if (x < 2.0) {
        if (fabs(x) < 1.3877787807814457e-17) return 1.0 - x;
        DoubleDouble e = erfSeries(fabs(x));
        return (x < 0 ? e.plus(1.0) : DoubleDouble(1.0, 0.0).minus(e)).value();
    }
    if (x > 27.3) {
        errno = ERANGE;
        return 0.0;
    }
    int k;
    DoubleDouble m = erfcFraction(x, k);
    return BinaryScale::scale(m.value(), k);
}

extern "C" double tgamma(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return x > 0 ? x : domain();
    if (x == 0.0) return overflow(MathBits::negative(u));
    if (x < 0 && x == floor(x)) return domain();
    if (x > 171.7) return overflow(false);
    if (fabs(x) < 5.551115123125783e-17) {
        double r = 1.0 / x;
        if (!MathBits::isFinite(MathBits::of(r))) return overflow(x < 0);
        return r - eulerGamma().hi;
    }
    int k;
    if (x >= 0.5) {
        DoubleDouble m = gammaOf(DoubleDouble(x, 0.0), k);
        return BinaryScale::scale(m.value(), k);
    }
    if (x < -190.0) {
        errno = ERANGE;
        return 0.0;
    }
    DoubleDouble g = gammaOf(DoubleDouble::sum(1.0, -x), k);
    double r = BinaryScale::scale(pi().over(sinPi(x).times(g)).value(), -k);
    return r == 0.0 ? 0.0 : r;
}

extern "C" double lgamma(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x + x;
    if (MathBits::isInfinite(u)) return MathBits::infinity(false);
    if (x == 0.0 || (x < 0 && x == floor(x))) return overflow(false);
    if (fabs(x) < 5.551115123125783e-17) return -logOf(DoubleDouble(fabs(x), 0.0)).value();
    if (x > 0) {
        if (fabs(x - 1.0) < 0.2) return nearRoot(x - 1.0, eulerGamma().negated(), zetaOverK);
        if (fabs(x - 2.0) < 0.2) return nearRoot(x - 2.0, oneLessGamma(), zetaLessOneOverK);
        if (x > 1e280) {
            double r = x * (log(x) - 1.0);
            if (!MathBits::isFinite(MathBits::of(r))) return overflow(false);
            return r;
        }
        return lnGammaOf(DoubleDouble(x, 0.0)).value();
    }
    DoubleDouble s = sinPi(x);
    if (s.hi < 0) s = s.negated();
    return lnPi().minus(logOf(s)).minus(lnGammaOf(DoubleDouble::sum(1.0, -x))).value();
}
