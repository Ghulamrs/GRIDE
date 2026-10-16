// Spec: IEEE 754 binary64 (sign, 11-bit exponent biased by 1023, 52-bit fraction, subnormals),
// and its exact decimal value: m x 2^e is m x 5^-e / 10^-e for e < 0 - an integer, then a shift.
// Base-10000 limbs keep every step in 32 bits, with constant divisors only.

#include "DecimalDigits.h"

namespace rts6x {

void DecimalDigits::multiply(unsigned *limbs, unsigned &n, unsigned factor)
{
    // factor at most 390625: 9999 x 390625 plus a carry stays below 2^32.
    unsigned carry = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned t = limbs[i] * factor + carry;
        carry = t / LimbBase;
        limbs[i] = t - carry * LimbBase;
    }
    while (carry != 0) {
        limbs[n++] = carry % LimbBase;
        carry /= LimbBase;
    }
}

DecimalDigits::DecimalDigits(double v) : count_(0), point_(1)
{
    union { double d; unsigned w[2]; } bits;
    bits.d = v;
    unsigned hi = bits.w[1] & 0x7FFFFFFFu, lo = bits.w[0];
    int biased = (int)(hi >> 20);
    unsigned top = hi & 0xFFFFFu;
    if (biased == 0) {
        if (top == 0 && lo == 0) return;
        biased = 1;
    } else {
        top |= 0x100000u;
    }
    int e = biased - 1075;

    // The 53-bit integer top:lo into base 10000, four 16-bit pieces at a time.
    unsigned limbs[MaxLimbs];
    unsigned n = 0;
    unsigned piece[4] = { top >> 16, top & 0xFFFFu, lo >> 16, lo & 0xFFFFu };
    while (piece[0] | piece[1] | piece[2] | piece[3]) {
        unsigned r = 0;
        for (int k = 0; k < 4; k++) {
            unsigned cur = r * 65536u + piece[k];
            piece[k] = cur / LimbBase;
            r = cur - piece[k] * LimbBase;
        }
        limbs[n++] = r;
    }

    int shift = 0;
    if (e > 0) {
        for (; e >= 16; e -= 16) multiply(limbs, n, 65536u);
        if (e > 0) multiply(limbs, n, 1u << e);
    } else if (e < 0) {
        shift = -e;
        int k = shift;
        for (; k >= 8; k -= 8) multiply(limbs, n, 390625u);
        unsigned five = 1;
        for (; k > 0; k--) five *= 5;
        if (five != 1) multiply(limbs, n, five);
    }

    // Most significant limb first, the top one without its leading zeros.
    char buf[4];
    for (int i = (int)n - 1; i >= 0; i--) {
        unsigned x = limbs[i];
        for (int k = 3; k >= 0; k--) { buf[k] = (char)('0' + x % 10); x /= 10; }
        for (int k = 0; k < 4; k++)
            if (count_ != 0 || buf[k] != '0') digits_[count_++] = buf[k];
    }
    point_ = (int)count_ - shift;
    trim();
}

void DecimalDigits::trim()
{
    while (count_ != 0 && digits_[count_ - 1] == '0') count_--;
}

bool DecimalDigits::roundsUp(int keep) const
{
    char d = digits_[keep];
    bool beyond = (int)count_ > keep + 1;
    bool odd = keep > 0 && ((digits_[keep - 1] - '0') & 1) != 0;
    return d > '5' || (d == '5' && (beyond || odd));
}

int DecimalDigits::pointAfterRound(int keep) const
{
    if (keep < 0 || keep >= (int)count_ || !roundsUp(keep)) return point_;
    for (int i = 0; i < keep; i++)
        if (digits_[i] != '9') return point_;
    return point_ + 1;
}

void DecimalDigits::round(int keep)
{
    if (keep >= (int)count_) return;
    if (keep < 0) { count_ = 0; return; }
    bool up = roundsUp(keep);
    count_ = (unsigned)keep;
    if (up) {
        int i = keep - 1;
        while (i >= 0 && digits_[i] == '9') digits_[i--] = '0';
        if (i >= 0) {
            digits_[i]++;
        } else {
            digits_[0] = '1';
            count_ = 1;
            point_++;
        }
    }
    trim();
}

}  // namespace rts6x
