// Spec: ISO C99 7.12 and F.9 - the functions of <math.h> that work on a double's bits, or round it to an integer, as
// the Microsoft runtime's UCRT gives them (its values, NaNs and errno: tests\math99):
//   copysign; fmax and fmin, a NaN argument passed over and of two zeros fmax's +0 and fmin's -0; fdim, with no
//   errno for an overflow; nan, the
//   default quiet NaN whatever its text; logb (0: -inf, ERANGE) and ilogb (0: INT_MIN; inf, NaN: INT_MAX; no errno);
//   scalbn and scalbln, ldexp's rounding and errno; nextafter and nexttoward, y when x == y, ERANGE for a result
//   that is infinite, subnormal or zero from a finite x; lrint, llrint, lround and llround, a NaN, an infinity or a
//   value outside the type 0 and EDOM (llrint and llround take 2^63 itself to LLONG_MIN, as that runtime does);
//   remainder and remquo, x - n y with n nearest x / y and ties to even, exactly, remquo's quotient its low 31 bits
//   with its sign, y zero or x infinite the quiet NaN and EDOM. rint and nearbyint are c\rint.asm.
#include <errno.h>
#include <limits.h>
#include <math.h>
#include "math/MathBits.h"
#include "math/BinaryScale.h"

using rts6x::MathBits;
using rts6x::BinaryScale;

namespace {

const unsigned long long signBit = 0x8000000000000000ull;

double quietNaN()
{
    return MathBits::from(0x7FF8000000000000ull);
}

// The exponent of a finite nonzero x: floor(log2 |x|), subnormals counted from their top bit.
int exponentOf(unsigned long long u)
{
    int e = MathBits::biased(u);
    if (e != 0) return e - 1023;
    unsigned long long f = u & MathBits::fractionMask();
    int top = 51;
    while (!(f >> top)) top--;
    return top - 1074;
}

// x = m 2^e with m in [2^52, 2^53): a finite nonzero x's significand made normal
void normalised(unsigned long long u, unsigned long long &m, int &e)
{
    int biased = MathBits::biased(u);
    m = u & MathBits::fractionMask();
    if (biased != 0) {
        m |= 1ull << 52;
        e = biased - 1075;
    } else {
        e = -1074;
        while (!(m >> 52)) {
            m <<= 1;
            e--;
        }
    }
}

double remainderOf(double x, double y, int *quotient)
{
    *quotient = 0;
    unsigned long long ux = MathBits::of(x), uy = MathBits::of(y);
    if (MathBits::isNaN(ux) || MathBits::isNaN(uy)) return x + y;
    if (MathBits::isInfinite(ux) || MathBits::isZero(uy)) {
        errno = EDOM;
        return quietNaN();
    }
    if (MathBits::isInfinite(uy) || MathBits::isZero(ux)) return x;
    unsigned long long mx, my;
    int ex, ey;
    normalised(ux, mx, ex);
    normalised(uy, my, ey);
    // |x| = q |y| + r 2^ey, by long division on the significands, q's low bits kept
    unsigned long long r = mx, q = 0;
    if (ex < ey) {
        // |x| < |y|: q is 0 and the remainder is |x|, in units of 2^ex
        r = 0;
    } else {
        for (int k = ex - ey; k > 0; k--) {
            q <<= 1;
            if (r >= my) {
                r -= my;
                q |= 1;
            }
            r <<= 1;
        }
        q <<= 1;
        if (r >= my) {
            r -= my;
            q |= 1;
        }
    }
    double magnitude;
    if (ex < ey) {
        // round: 2|x| against |y|, both exact as doubles here
        double ax = BinaryScale::magnitude(x), ay = BinaryScale::magnitude(y);
        if (ax > ay - ax) {
            magnitude = ax - ay;
            q = 1;
        } else {
            magnitude = ax;
        }
    } else {
        bool up = 2 * r > my || (2 * r == my && (q & 1));
        if (up) q++;
        double whole = up ? -(double)(long long)(my - r) : (double)(long long)r;
        magnitude = BinaryScale::scale(whole, ey);
    }
    int sign = MathBits::negative(ux) != MathBits::negative(uy) ? -1 : 1;
    *quotient = sign * (int)(q & 0x7FFFFFFF);
    return MathBits::negative(ux) ? -magnitude : magnitude;
}

}  // namespace

extern "C" double copysign(double x, double y)
{
    return MathBits::from((MathBits::of(x) & ~signBit) | (MathBits::of(y) & signBit));
}

extern "C" double fmax(double x, double y)
{
    if (x != x) return y;
    if (y != y) return x;
    if (x == y) return MathBits::from(MathBits::of(x) & MathBits::of(y));
    return x > y ? x : y;
}

extern "C" double fmin(double x, double y)
{
    if (x != x) return y;
    if (y != y) return x;
    if (x == y) return MathBits::from(MathBits::of(x) | MathBits::of(y));
    return x < y ? x : y;
}

extern "C" double fdim(double x, double y)
{
    if (x != x) return x;
    if (y != y) return y;
    return x > y ? x - y : 0.0;
}

extern "C" double nan(const char *)
{
    return quietNaN();
}

extern "C" double logb(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isNaN(u)) return x;
    if (MathBits::isInfinite(u)) return MathBits::infinity(false);
    if (MathBits::isZero(u)) {
        errno = ERANGE;
        return MathBits::infinity(true);
    }
    return (double)exponentOf(u);
}

extern "C" int ilogb(double x)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isZero(u)) return INT_MIN;
    if (!MathBits::isFinite(u)) return INT_MAX;
    return exponentOf(u);
}

extern "C" double scalbn(double x, int n)
{
    return BinaryScale::scale(x, n);
}

extern "C" double scalbln(double x, long n)
{
    return BinaryScale::scale(x, (int)n);
}

extern "C" double nextafter(double x, double y)
{
    if (x != x || y != y) return x + y;
    if (x == y) return y;
    unsigned long long u = MathBits::of(x);
    if (MathBits::isZero(u)) {
        errno = ERANGE;
        return MathBits::from((MathBits::of(y) & signBit) | 1);
    }
    if ((x < y) == (x > 0)) u++;
    else u--;
    double r = MathBits::from(u);
    unsigned long long v = MathBits::of(r);
    if (MathBits::isFinite(MathBits::of(x)) && (!MathBits::isFinite(v) || MathBits::biased(v) == 0)) errno = ERANGE;
    return r;
}

extern "C" double nexttoward(double x, long double y)
{
    return nextafter(x, (double)y);
}

extern "C" long lrint(double x)
{
    double r = rint(x);
    if (r != r || r > 2147483647.0 || r < -2147483648.0) {
        errno = EDOM;
        return 0;
    }
    return (long)r;
}

extern "C" long long llrint(double x)
{
    double r = rint(x);
    if (r != r || r > 9223372036854775807.0 || r < -9223372036854775807.0 - 1.0) {
        errno = EDOM;
        return 0;
    }
    return (long long)r;
}

extern "C" long lround(double x)
{
    double r = round(x);
    if (r != r || r > 2147483647.0 || r < -2147483648.0) {
        errno = EDOM;
        return 0;
    }
    return (long)r;
}

extern "C" long long llround(double x)
{
    double r = round(x);
    if (r != r || r > 9223372036854775807.0 || r < -9223372036854775807.0 - 1.0) {
        errno = EDOM;
        return 0;
    }
    return (long long)r;
}

// The UCRT's remainder, unlike its remquo, answers an infinite x with a NaN of x's sign and EDOM, y a NaN or not.
extern "C" double remainder(double x, double y)
{
    unsigned long long u = MathBits::of(x);
    if (MathBits::isInfinite(u)) {
        errno = EDOM;
        return MathBits::from(0x7FF8000000000000ull | (u & signBit));
    }
    int q;
    return remainderOf(x, y, &q);
}

extern "C" double remquo(double x, double y, int *quotient)
{
    return remainderOf(x, y, quotient);
}
