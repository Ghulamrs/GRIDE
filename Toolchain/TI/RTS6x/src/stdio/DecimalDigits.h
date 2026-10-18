// Spec: ISO C 7.19.6.1/8 (f, e, g: the decimal digits of the value, rounded) and F.5 / IEEE 754
// (conversion correctly rounded, to nearest, ties to even). The exact digits of a finite double -
// every binary fraction is a finite decimal one - so rounding them is exact too.
#ifndef RTS6X_DECIMAL_DIGITS_H
#define RTS6X_DECIMAL_DIGITS_H

namespace rts6x {

class DecimalDigits {
public:
    // |v|, v finite: digits d1 d2 ... dn (no trailing zeros) with value 0.d1d2...dn x 10^point.
    explicit DecimalDigits(double v);

    // Keeps `keep` digits (0 or fewer allowed), the rest rounded to nearest, ties to even.
    void round(int keep);
    // Where the point would be after round(keep), without rounding: %g chooses its style by it.
    int pointAfterRound(int keep) const;

    bool zero() const { return count_ == 0; }
    int count() const { return (int)count_; }
    int point() const { return point_; }
    // The digit at position i of the sequence, as a character; '0' outside it.
    char digit(int i) const { return i >= 0 && i < (int)count_ ? digits_[i] : '0'; }

private:
    // 2^-1074 times a 53-bit integer has at most 767 significant digits.
    enum { MaxDigits = 800, MaxLimbs = 200, LimbBase = 10000 };

    // limbs: the integer in base 10000, least significant first.
    // Whether round(keep) would round up: the first dropped digit above 5, or 5 with more after it
    // or an odd digit before it.
    bool roundsUp(int keep) const;
    void multiply(unsigned *limbs, unsigned &n, unsigned factor);
    void trim();

    char digits_[MaxDigits];
    unsigned count_;
    int point_;
};

}  // namespace rts6x

#endif
