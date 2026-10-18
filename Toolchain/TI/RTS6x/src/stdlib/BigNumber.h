// Spec: arithmetic on unsigned integers of a few thousand bits, base 2^32 limbs, least significant
// first - what an exact decimal-to-binary conversion needs: multiply by a word, shift, compare,
// subtract. A fixed capacity: strtod bounds the integers it builds (FloatText.h says how).
#ifndef RTS6X_BIG_NUMBER_H
#define RTS6X_BIG_NUMBER_H

namespace rts6x {

class BigNumber {
public:
    enum { Limbs = 128 };

    // No constructor, so a BigNumber in static storage is zero-filled data and needs no code to build.
    void set(unsigned long long v);

    bool zero() const { return used_ == 0; }
    // The number of bits from the highest set one down; 0 for zero.
    int bitLength() const;
    // The 64 bits below and including the highest set one, and whether any bit under them is set.
    unsigned long long top64(bool &sticky) const;
    unsigned long long low64() const;

    // this = this * factor + addend.
    void multiplyAdd(unsigned factor, unsigned addend);
    // this = this * 10^n.
    void multiplyPowerOfTen(int n);
    void shiftLeft(int bits);
    void shiftRight(int bits);
    // -1, 0 or 1 as this is less than, equal to or greater than other.
    int compare(const BigNumber &other) const;
    // this -= other, other being no greater.
    void subtract(const BigNumber &other);

private:
    void trim() { while (used_ > 0 && limbs_[used_ - 1] == 0) used_--; }

    unsigned limbs_[Limbs];
    int used_;
};

}  // namespace rts6x

#endif
