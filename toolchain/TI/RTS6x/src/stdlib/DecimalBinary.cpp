// Spec: w x 10^e = w 5^b 5^(28a) 2^e. X = w 5^b is exact in 128 bits, P = X C in 192 (Knuth, TAOCP
// vol. 2, 4.3.1). With C = floor(5^(28a) 2^-k) >= 2^63 the true X 5^(28a) 2^-k is in [P, P + X), under
// two units of P's top 64 bits S: rounding is open only where S's last 11 bits are 0x3FE-F or 0x7FE-F.

#include "DecimalBinary.h"

namespace rts6x {

int DecimalBinary::bitLength(unsigned x)
{
    int n = 0;
    if (x >> 16) { n += 16; x >>= 16; }
    if (x >> 8) { n += 8; x >>= 8; }
    if (x >> 4) { n += 4; x >>= 4; }
    if (x >> 2) { n += 2; x >>= 2; }
    if (x >> 1) { n += 1; x >>= 1; }
    return n + (int)x;
}

bool DecimalBinary::toBinary64(unsigned long long w, int e, bool negative, unsigned long long &bits)
{
    if (e < -364 || e > 335 || w == 0) return false;
    int a = (e + 364) / 28, b = e + 364 - a * 28;
    // X = w 5^b, four limbs, then P = X C, six and a zero: written out, each step a 32 x 32 + 32 + 32
    // product split into its words - the loops' indexing cost more than the arithmetic.
    union { unsigned long long u; unsigned word[2]; } wv, fv, t;
    wv.u = w;
    fv.u = fives[b];
    unsigned c0 = powers[a].limbs[0], c1 = powers[a].limbs[1], x[4], p[6];
    t.u = product(wv.word[0], fv.word[0]);
    x[0] = t.word[0];
    t.u = product(wv.word[0], fv.word[1]) + t.word[1];
    x[1] = t.word[0];
    x[2] = t.word[1];
    t.u = product(wv.word[1], fv.word[0]) + x[1];
    x[1] = t.word[0];
    t.u = product(wv.word[1], fv.word[1]) + x[2] + t.word[1];
    x[2] = t.word[0];
    x[3] = t.word[1];
    t.u = product(x[0], c0);
    p[0] = t.word[0];
    t.u = product(x[0], c1) + t.word[1];
    p[1] = t.word[0];
    p[2] = t.word[1];
    t.u = product(x[1], c0) + p[1];
    p[1] = t.word[0];
    t.u = product(x[1], c1) + p[2] + t.word[1];
    p[2] = t.word[0];
    p[3] = t.word[1];
    t.u = product(x[2], c0) + p[2];
    p[2] = t.word[0];
    t.u = product(x[2], c1) + p[3] + t.word[1];
    p[3] = t.word[0];
    p[4] = t.word[1];
    t.u = product(x[3], c0) + p[3];
    p[3] = t.word[0];
    t.u = product(x[3], c1) + p[4] + t.word[1];
    p[4] = t.word[0];
    p[5] = t.word[1];
    // S, the 64 bits from P's highest set one down; sticky, any set bit below them.
    int top = 5;
    while (p[top] == 0) top--;
    int length = top * 32 + bitLength(p[top]), from = length - 64, limb = from >> 5, shift = from & 31;
    // hi is past P's top limb when S starts in p[4]; that read is 0.
    unsigned lo = p[limb], mid = p[limb + 1], hi = limb + 2 < 6 ? p[limb + 2] : 0;
    unsigned sticky = shift ? lo << (32 - shift) : 0;
    for (int i = 0; i < limb; i++) sticky |= p[i];
    union { unsigned long long u; unsigned word[2]; } s;
    s.word[0] = shift ? (lo >> shift) | (mid << (32 - shift)) : lo;
    s.word[1] = shift ? (mid >> shift) | (hi << (32 - shift)) : mid;
    unsigned last = s.word[0] & 0x7FF;
    bool exact = powers[a].exact != 0;
    if (!exact && ((last & 0x3FE) == 0x3FE)) return false;
    // The value is S 2^exponent, S's top bit at 63; a normal binary64 below the top binade.
    int exponent = from + powers[a].k + e, biased = exponent + 63 + 1023;
    if (biased < 1 || biased > 2045) return false;
    // The top 53 bits, rounded to nearest even on the 54th and everything below it; a carry out of
    // the significand moves into the exponent field by the addition itself.
    unsigned high = s.word[1] >> 11, low = (s.word[1] << 21) | (s.word[0] >> 11);
    bool rest = (s.word[0] & 0x3FF) != 0 || sticky != 0 || !exact;
    if ((s.word[0] & 0x400) != 0 && (rest || (low & 1) != 0)) {
        low++;
        if (low == 0) high++;
    }
    s.word[1] = (negative ? 0x80000000u : 0) | (((unsigned)biased << 20) + high - 0x100000);
    s.word[0] = low;
    bits = s.u;
    return true;
}

}  // namespace rts6x
