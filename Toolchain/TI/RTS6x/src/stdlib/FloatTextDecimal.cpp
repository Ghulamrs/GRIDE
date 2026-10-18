// Spec: ISO C 7.20.1.3/5 and F.5 - a decimal converted correctly rounded: FloatTextShort's one rounding
// where it is exact; else the digits' integer D times 10^e formed whole for e >= 0, and for e < 0,
// D x 2^k divided by 10^-e with k chosen to leave a 63- or 64-bit quotient and a remainder.

#include "FloatText.h"
#include "BigNumber.h"
#include "DecimalBinary.h"
#include "../ctype/CharacterClass.h"

namespace rts6x {

namespace {

// The integers are large; they live here rather than on the stack, the C6747 running one thread.
BigNumber numerator, denominator;

}  // namespace

const char *FloatText::exponentPart(const char *p, int &exponent)
{
    if (*p != 'e' && *p != 'E') return p;
    const char *q = p + 1;
    bool down = false;
    if (*q == '+' || *q == '-') down = *q++ == '-';
    if (!CharacterClass::digit((unsigned char)*q)) return p;
    long e = 0;
    for (; CharacterClass::digit((unsigned char)*q); q++)
        if (e < 100000) e = e * 10 + (*q - '0');
    exponent += down ? -(int)e : (int)e;
    return q;
}

unsigned long long FloatText::decimal(const FloatFormat &format, bool negative, const char *s,
                                      const char **end, bool &range)
{
    // The short way first: 19 significant digits at most in a 64-bit w, zeros past them only.
    const char *p = s;
    int count = 0, exponent = 0;
    bool point = false, more = false;
    unsigned long long w = 0;
    for (;; p++) {
        if (*p == '.' && !point) { point = true; continue; }
        unsigned digit = (unsigned)(*p - '0');
        if (digit > 9) break;
        if (count == 0 && digit == 0) {
            if (point) exponent--;
        } else if (count < 19) {
            w = w * 10 + digit;
            count++;
            if (point) exponent--;
        } else {
            more = more || digit != 0;
            if (!point) exponent++;
        }
    }
    *end = exponentPart(p, exponent);
    if (count == 0) return format.zero(negative);
    unsigned long long bits;
    if (!more && shortWay(format, negative, w, exponent, bits)) return bits;
    if (!more && format.precision() == 53 && DecimalBinary::toBinary64(w, exponent, negative, bits)) return bits;

    // The long way: every digit up to MaxDigits in a BigNumber.
    p = s;
    count = 0;
    exponent = 0;
    bool sticky = false;
    point = false;
    numerator.set(0);
    for (;; p++) {
        if (*p == '.' && !point) { point = true; continue; }
        if (!CharacterClass::digit((unsigned char)*p)) break;
        int digit = *p - '0';
        if (count == 0 && digit == 0) {
            if (point) exponent--;
            continue;
        }
        if (count < MaxDigits) {
            numerator.multiplyAdd(10, (unsigned)digit);
            count++;
            if (point) exponent--;
        } else {
            sticky = sticky || digit != 0;
            if (!point) exponent++;
        }
    }
    exponentPart(p, exponent);
    return scale(format, negative, numerator, count, exponent, sticky, range);
}

unsigned long long FloatText::scale(const FloatFormat &format, bool negative, BigNumber &d, int count,
                                    int exponent, bool sticky, bool &range)
{
    if (count == 0) return format.zero(negative);
    // The value lies in [10^(count+exponent-1), 10^(count+exponent)).
    if (count + exponent - 1 > 310) { range = true; return format.infinity(negative); }
    if (count + exponent < -326) { range = true; return format.zero(negative); }
    unsigned long long bits;
    if (exponent >= 0) {
        d.multiplyPowerOfTen(exponent);
        bool lost;
        int length = d.bitLength();
        unsigned long long top = d.top64(lost);
        bits = FloatPacker::pack(format, negative, length > 64 ? length - 64 : 0, top, lost || sticky);
    } else {
        denominator.set(1);
        denominator.multiplyPowerOfTen(-exponent);
        int k = denominator.bitLength() - d.bitLength() + 63;
        if (k > 0) d.shiftLeft(k);
        else denominator.shiftLeft(-k);
        // One quotient bit at a time: q < 2^64, as k was chosen.
        int dbits = denominator.bitLength();
        int nbits = d.bitLength();
        int shift = nbits - dbits;
        if (shift < 0) shift = 0;
        denominator.shiftLeft(shift);
        unsigned long long q = 0;
        for (int i = shift; i >= 0; i--) {
            q <<= 1;
            if (d.compare(denominator) >= 0) {
                d.subtract(denominator);
                q |= 1;
            }
            denominator.shiftRight(1);
        }
        bits = FloatPacker::pack(format, negative, -k, q, !d.zero() || sticky);
    }
    // Overflow, or a subnormal result: 7.20.1.3/10 lets ERANGE say underflow whether or not the value was exact.
    unsigned long long magnitude = bits & ~format.signBit();
    if (magnitude < (1ull << format.fractionBits()) || magnitude == format.infinity(false)) range = true;
    return bits;
}

}  // namespace rts6x
