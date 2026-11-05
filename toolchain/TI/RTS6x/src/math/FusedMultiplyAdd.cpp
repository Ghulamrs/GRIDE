// Spec: ISO C99 7.12.13.1 fma and F.9.10.1 - x * y + z rounded once to nearest, ties to even, by integers.
// The significands' product is exact in 106 bits. It and z are added in a 256-bit window that holds the larger of
// the two whole, its top at bit 200; whatever of the smaller lies below the window is kept as a sticky bit at the
// window's bottom, far beneath the place the sum is rounded at, so the rounding is that of the exact sum.

#include "FusedMultiply.h"
#include "MathBits.h"

using rts6x::MathBits;

namespace {

struct Wide {
    unsigned long long w[4];    // w[0] the least significant
};

void clear(Wide &a)
{
    a.w[0] = a.w[1] = a.w[2] = a.w[3] = 0;
}

bool isZero(const Wide &a)
{
    return !(a.w[0] | a.w[1] | a.w[2] | a.w[3]);
}

int topBit(const Wide &a)
{
    for (int i = 3; i >= 0; i--)
        if (a.w[i]) {
            int b = 63;
            while (!(a.w[i] >> b)) b--;
            return i * 64 + b;
        }
    return -1;
}

bool bitAt(const Wide &a, int n)
{
    return n >= 0 && n < 256 && ((a.w[n >> 6] >> (n & 63)) & 1);
}

// a bit set in a below position n
bool anyBelow(const Wide &a, int n)
{
    for (int i = 0; i < 4 && i * 64 < n; i++) {
        unsigned long long word = a.w[i];
        int count = n - i * 64;
        if (count < 64) word &= (1ull << count) - 1;
        if (word) return true;
    }
    return false;
}

void shiftLeft(Wide &a, int n)
{
    int words = n >> 6, bits = n & 63;
    for (int i = 3; i >= 0; i--) {
        unsigned long long v = 0;
        int from = i - words;
        if (from >= 0) {
            v = a.w[from] << bits;
            if (bits && from > 0) v |= a.w[from - 1] >> (64 - bits);
        }
        a.w[i] = v;
    }
}

// a >> n, any bit shifted out kept as bit 0
void shiftRightSticky(Wide &a, int n)
{
    if (n >= 256) {
        bool any = !isZero(a);
        clear(a);
        a.w[0] = any ? 1 : 0;
        return;
    }
    bool sticky = anyBelow(a, n);
    int words = n >> 6, bits = n & 63;
    for (int i = 0; i < 4; i++) {
        unsigned long long v = 0;
        int from = i + words;
        if (from < 4) {
            v = a.w[from] >> bits;
            if (bits && from + 1 < 4) v |= a.w[from + 1] << (64 - bits);
        }
        a.w[i] = v;
    }
    if (sticky) a.w[0] |= 1;
}

int compare(const Wide &a, const Wide &b)
{
    for (int i = 3; i >= 0; i--)
        if (a.w[i] != b.w[i]) return a.w[i] < b.w[i] ? -1 : 1;
    return 0;
}

void add(Wide &a, const Wide &b)
{
    unsigned long long carry = 0;
    for (int i = 0; i < 4; i++) {
        unsigned long long s = a.w[i] + b.w[i];
        unsigned long long c = s < a.w[i] ? 1 : 0;
        a.w[i] = s + carry;
        if (a.w[i] < s) c = 1;
        carry = c;
    }
}

// a - b, a >= b
void subtract(Wide &a, const Wide &b)
{
    unsigned long long borrow = 0;
    for (int i = 0; i < 4; i++) {
        unsigned long long d = a.w[i] - b.w[i];
        unsigned long long c = a.w[i] < b.w[i] ? 1 : 0;
        if (d < borrow) c = 1;
        a.w[i] = d - borrow;
        borrow = c;
    }
}

// the 64 bits of a from bit lsb up
unsigned long long bitsFrom(const Wide &a, int lsb)
{
    int word = lsb >> 6, b = lsb & 63;
    unsigned long long v = word < 4 ? a.w[word] >> b : 0;
    if (b && word + 1 < 4) v |= a.w[word + 1] << (64 - b);
    return v;
}

void multiply(unsigned long long a, unsigned long long b, unsigned long long &hi, unsigned long long &lo)
{
    unsigned long long a0 = a & 0xFFFFFFFFull, a1 = a >> 32, b0 = b & 0xFFFFFFFFull, b1 = b >> 32;
    unsigned long long p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    unsigned long long middle = (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);
    lo = (middle << 32) | (p00 & 0xFFFFFFFFull);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
}

// a finite nonzero double as m 2^e, m below 2^53
void parts(unsigned long long u, unsigned long long &m, int &e)
{
    int biased = MathBits::biased(u);
    m = u & MathBits::fractionMask();
    if (biased) {
        m |= 1ull << 52;
        e = biased - 1075;
    } else {
        e = -1074;
    }
}

// a placed in the window whose bit 0 stands for 2^base, a's bit 0 standing for 2^e
void place(Wide &a, int e, int base)
{
    int at = e - base;
    if (at >= 0) shiftLeft(a, at);
    else shiftRightSticky(a, -at);
}

}  // namespace

namespace rts6x {

double FusedMultiply::evaluate(double x, double y, double z)
{
    unsigned long long ux = MathBits::of(x), uy = MathBits::of(y), uz = MathBits::of(z);
    if (!MathBits::isFinite(ux) || !MathBits::isFinite(uy) || !MathBits::isFinite(uz)) {
        // a finite product with an infinite z is z, though x * y may overflow on its way there
        if (MathBits::isFinite(ux) && MathBits::isFinite(uy) && MathBits::isInfinite(uz)) return z;
        return x * y + z;
    }
    if (MathBits::isZero(ux) || MathBits::isZero(uy)) return x * y + z;
    if (MathBits::isZero(uz)) return x * y;
    bool productNegative = MathBits::negative(ux) != MathBits::negative(uy), zNegative = MathBits::negative(uz);
    unsigned long long mx, my, mz;
    int ex, ey, ez;
    parts(ux, mx, ex);
    parts(uy, my, ey);
    parts(uz, mz, ez);
    Wide p, q;
    clear(p);
    clear(q);
    multiply(mx, my, p.w[1], p.w[0]);
    q.w[0] = mz;
    int ep = ex + ey;
    int top = ep + topBit(p), topZ = ez + topBit(q);
    int base = (top > topZ ? top : topZ) - 200;
    place(p, ep, base);
    place(q, ez, base);
    bool negative;
    if (productNegative == zNegative) {
        add(p, q);
        negative = productNegative;
    } else if (compare(p, q) >= 0) {
        subtract(p, q);
        negative = productNegative;
    } else {
        subtract(q, p);
        p = q;
        negative = zNegative;
    }
    if (isZero(p)) return 0.0;
    unsigned long long sign = negative ? 0x8000000000000000ull : 0;
    int t = topBit(p);
    if (t + base > 1023) return MathBits::from(sign | 0x7FF0000000000000ull);
    // the result's last bit: 52 below its first, or 2^-1074 for a subnormal one
    int lsb = t - 52;
    if (t + base < -1022) lsb = -1074 - base;
    unsigned long long m;
    if (lsb < 0) {
        m = p.w[0] << -lsb;     // fewer than 53 bits: exact
    } else {
        m = lsb > t ? 0 : bitsFrom(p, lsb);
        if (bitAt(p, lsb - 1) && (anyBelow(p, lsb - 1) || (m & 1))) m++;
    }
    int e = lsb + base;
    if (m == 1ull << 53) {
        m >>= 1;
        e++;
    }
    if (m == 0) return MathBits::from(sign);
    if (m < 1ull << 52) return MathBits::from(sign | m);
    int biased = e + 1075;
    if (biased >= 2047) return MathBits::from(sign | 0x7FF0000000000000ull);
    return MathBits::from(sign | ((unsigned long long)biased << 52) | (m & MathBits::fractionMask()));
}

}  // namespace rts6x
