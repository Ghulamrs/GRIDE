// Spec: SPRAB89B 8.2 Table 8-6 (__c6xabi_divull, remull, divlli, remlli: 64-bit / and %, C's
// truncation toward zero, ISO C 6.5.5/6). Long division in 32-bit pieces (Knuth, TAOCP vol. 2, 4.3.1,
// Algorithm D on base 2^16 digits): 32-bit divisions only, which go to the fast SUBC helpers.
#ifndef RTS6X_WIDE_DIVISION_H
#define RTS6X_WIDE_DIVISION_H

namespace rts6x {

class WideDivision {
public:
    // n / d, with n % d left in remainder; d is never 0 here (C leaves that undefined).
    static unsigned long long divide(unsigned long long n, unsigned long long d, unsigned long long &remainder);
    // The same for signed operands: the quotient truncated toward zero, the remainder with n's sign.
    static long long divide(long long n, long long d, long long &remainder);

private:
    // (high * 2^32 + low) / v when high < v, so the quotient fits 32 bits; the remainder in remainder.
    static unsigned divideLong(unsigned high, unsigned low, unsigned v, unsigned &remainder);
    // One base-2^16 digit of Algorithm D: (top * 2^16 + next) / v, v normalised, top < v.
    static unsigned digit(unsigned top, unsigned next, unsigned v, unsigned &remainder);
    // The number of zero bits above the highest one, 32 for 0.
    static int leadingZeros(unsigned v);
    static unsigned high(unsigned long long v) { return (unsigned)(v >> 32); }
    // All ones for a negative value, else zero; (x ^ s) - s negates x where s is all ones.
    static unsigned long long signMask(long long v) { return v < 0 ? ~0ull : 0ull; }
    static unsigned long long negateWhere(unsigned long long x, unsigned long long s) { return (x ^ s) - s; }
};

}  // namespace rts6x

#endif
