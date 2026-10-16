// Spec: schoolbook multi-precision arithmetic on base 2^32 digits (Knuth, TAOCP vol. 2, 4.3.1):
// multiplication by one digit, shifts, comparison and subtraction with borrow.

#include "BigNumber.h"

namespace rts6x {

void BigNumber::set(unsigned long long v)
{
    limbs_[0] = (unsigned)v;
    limbs_[1] = (unsigned)(v >> 32);
    used_ = 2;
    trim();
}

int BigNumber::bitLength() const
{
    if (used_ == 0) return 0;
    unsigned top = limbs_[used_ - 1];
    int bits = 0;
    while (top) { bits++; top >>= 1; }
    return (used_ - 1) * 32 + bits;
}

unsigned long long BigNumber::low64() const
{
    unsigned long long v = used_ > 0 ? limbs_[0] : 0;
    if (used_ > 1) v |= (unsigned long long)limbs_[1] << 32;
    return v;
}

unsigned long long BigNumber::top64(bool &sticky) const
{
    int length = bitLength();
    sticky = false;
    if (length <= 64) return low64();
    BigNumber copy(*this);
    int drop = length - 64;
    for (int i = 0; i < drop / 32 && !sticky; i++) sticky = limbs_[i] != 0;
    if (!sticky && drop % 32) sticky = (limbs_[drop / 32] & ((1u << (drop % 32)) - 1)) != 0;
    copy.shiftRight(drop);
    return copy.low64();
}

void BigNumber::multiplyAdd(unsigned factor, unsigned addend)
{
    unsigned long long carry = addend;
    for (int i = 0; i < used_; i++) {
        unsigned long long p = (unsigned long long)limbs_[i] * factor + carry;
        limbs_[i] = (unsigned)p;
        carry = p >> 32;
    }
    if (carry && used_ < Limbs) limbs_[used_++] = (unsigned)carry;
    trim();
}

void BigNumber::multiplyPowerOfTen(int n)
{
    for (; n >= 9; n -= 9) multiplyAdd(1000000000u, 0);
    unsigned rest = 1;
    for (; n > 0; n--) rest *= 10;
    if (rest > 1) multiplyAdd(rest, 0);
}

void BigNumber::shiftLeft(int bits)
{
    if (used_ == 0 || bits <= 0) return;
    int words = bits / 32, part = bits % 32;
    int top = used_ + words + 1;
    if (top > Limbs) top = Limbs;
    for (int i = top - 1; i >= 0; i--) {
        int from = i - words;
        unsigned hi = from >= 0 && from < used_ ? limbs_[from] : 0;
        unsigned lo = from - 1 >= 0 && from - 1 < used_ ? limbs_[from - 1] : 0;
        limbs_[i] = part ? (hi << part) | (lo >> (32 - part)) : hi;
    }
    used_ = top;
    trim();
}

void BigNumber::shiftRight(int bits)
{
    if (bits <= 0) return;
    int words = bits / 32, part = bits % 32;
    for (int i = 0; i < used_; i++) {
        int from = i + words;
        unsigned lo = from < used_ ? limbs_[from] : 0;
        unsigned hi = from + 1 < used_ ? limbs_[from + 1] : 0;
        limbs_[i] = part ? (lo >> part) | (hi << (32 - part)) : lo;
    }
    used_ = used_ > words ? used_ - words : 0;
    trim();
}

int BigNumber::compare(const BigNumber &other) const
{
    if (used_ != other.used_) return used_ < other.used_ ? -1 : 1;
    for (int i = used_ - 1; i >= 0; i--)
        if (limbs_[i] != other.limbs_[i]) return limbs_[i] < other.limbs_[i] ? -1 : 1;
    return 0;
}

void BigNumber::subtract(const BigNumber &other)
{
    unsigned borrow = 0;
    for (int i = 0; i < used_; i++) {
        unsigned long long d = (unsigned long long)limbs_[i] - (i < other.used_ ? other.limbs_[i] : 0) - borrow;
        limbs_[i] = (unsigned)d;
        borrow = (unsigned)(d >> 63);
    }
    trim();
}

}  // namespace rts6x
