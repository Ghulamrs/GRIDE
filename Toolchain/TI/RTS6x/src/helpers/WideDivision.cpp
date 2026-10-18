// Spec: ISO C 6.5.5/6 - integer division truncates toward zero, and (a/b)*b + a%b == a; the long
// division is Knuth's Algorithm D (TAOCP vol. 2, 4.3.1) with base 2^16 digits and a 32-bit divisor.

#include "WideDivision.h"

namespace rts6x {

int WideDivision::leadingZeros(unsigned v)
{
    if (v == 0) return 32;
    int n = 0;
    if ((v >> 16) == 0) { n += 16; v <<= 16; }
    if ((v >> 24) == 0) { n += 8; v <<= 8; }
    if ((v >> 28) == 0) { n += 4; v <<= 4; }
    if ((v >> 30) == 0) { n += 2; v <<= 2; }
    if ((v >> 31) == 0) n += 1;
    return n;
}

unsigned WideDivision::digit(unsigned top, unsigned next, unsigned v, unsigned &remainder)
{
    // The estimate from v's high half is at most two too large (step D3); the test on v's low half
    // takes it down while the estimate times v exceeds the two digits and the next one.
    unsigned vHigh = v >> 16, vLow = v & 0xFFFFu;
    unsigned q = top / vHigh;
    unsigned rest = top - q * vHigh;
    while (q > 0xFFFFu || q * vLow > ((rest << 16) | next)) {
        q--;
        rest += vHigh;
        if (rest > 0xFFFFu) break;
    }
    // Step D4: the three-digit value less q * v, which the corrected q leaves below v (mod 2^32).
    remainder = ((top << 16) | next) - q * v;
    return q;
}

unsigned WideDivision::divideLong(unsigned high, unsigned low, unsigned v, unsigned &remainder)
{
    // Step D1: v shifted until its top bit is set, the dividend with it; high < v keeps it in 64 bits.
    int s = leadingZeros(v);
    v <<= s;
    high = (high << s) | ((low >> 1) >> (31 - s));
    low <<= s;
    unsigned r, q1 = digit(high, low >> 16, v, r);
    unsigned q0 = digit(r, low & 0xFFFFu, v, r);
    remainder = r >> s;
    return (q1 << 16) | q0;
}

unsigned long long WideDivision::divide(unsigned long long n, unsigned long long d, unsigned long long &remainder)
{
    unsigned nHigh = high(n), nLow = (unsigned)n, dHigh = high(d), dLow = (unsigned)d;
    if (dHigh == 0) {
        if (nHigh == 0) {
            unsigned q = nLow / dLow;
            remainder = nLow - q * dLow;
            return q;
        }
        // The high word divided first; what is left of it is below dLow, as divideLong asks.
        unsigned qHigh = 0, r;
        if (nHigh >= dLow) { qHigh = nHigh / dLow; nHigh -= qHigh * dLow; }
        unsigned qLow = divideLong(nHigh, nLow, dLow, r);
        remainder = r;
        return ((unsigned long long)qHigh << 32) | qLow;
    }
    // d >= 2^32, so the quotient fits 32 bits. Its estimate from n / 2 and d's normalised top word is
    // the quotient or one more; one less than it is checked against the remainder it leaves.
    int s = leadingZeros(dHigh);
    unsigned top = (unsigned)((d << s) >> 32), unused;
    unsigned long long half = n >> 1;
    unsigned q = divideLong(high(half), (unsigned)half, top, unused) >> (31 - s);
    if (q != 0) q--;
    unsigned long long r = n - (unsigned long long)q * d;
    if (r >= d) { q++; r -= d; }
    remainder = r;
    return q;
}

long long WideDivision::divide(long long n, long long d, long long &remainder)
{
    // Branch-free signs: cpp11 left a branch's end label undefined on the C6000 at -O2 (2026-10-06).
    unsigned long long sn = signMask(n), sd = signMask(d), r;
    unsigned long long q = divide(negateWhere((unsigned long long)n, sn), negateWhere((unsigned long long)d, sd), r);
    remainder = (long long)negateWhere(r, sn);
    return (long long)negateWhere(q, sn ^ sd);
}

}  // namespace rts6x
