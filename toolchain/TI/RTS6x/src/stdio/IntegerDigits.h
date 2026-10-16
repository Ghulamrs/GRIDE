// Spec: ISO C 7.19.6.1/8 (d, i, o, u, x, X: the value in decimal, octal or hexadecimal). The digits
// of a 64-bit magnitude worked in 32-bit pieces, so no 64-bit division helper is called.
#ifndef RTS6X_INTEGER_DIGITS_H
#define RTS6X_INTEGER_DIGITS_H

namespace rts6x {

class IntegerDigits {
public:
    // The magnitude hi:lo in base 8, 10 or 16; upper picks A-F. No digits at all for zero.
    IntegerDigits(unsigned hi, unsigned lo, unsigned base, bool upper);

    unsigned length() const { return length_; }
    const char *text() const { return text_ + sizeof text_ - length_; }

private:
    // 2^64 has 22 octal digits, the most of the three bases. pairs_: "00" to "99".
    static const char pairs_[201];
    char text_[24];
    unsigned length_;
};

}  // namespace rts6x

#endif
