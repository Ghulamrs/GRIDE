// Spec: ISO C 7.20.1.4 - strtol and strtoul: white space, a sign, then digits in the base (2-36,
// or 0: decimal, 0 octal, 0x hexadecimal); the end where the number stopped, or the start if
// there was none; past the range, the nearest end of it and ERANGE.
#ifndef RTS6X_NUMBER_TEXT_H
#define RTS6X_NUMBER_TEXT_H

namespace rts6x {

// strtol's and strtoul's conversion (atoi's and atol's is NumberTextDecimal.h). One call from each
// entry point and none below it: cpp11 spends about twenty cycles on a call layer.
class NumberText {
public:
    // strtol (isSigned 1), strtoul (0): sign applied, ERANGE and the nearest limit past the range.
    static unsigned long convert(const unsigned char *s, char **end, int base, unsigned isSigned);

private:
    // Each character's digit value, 0-35 for 0-9, a-z and A-Z; 99 for every other character.
    static const unsigned char digit_[256];
    // How many digits of each base always fit in 32 bits, and the largest magnitude each base may
    // still multiply without passing them; bases 0 to 36 (and three spare bytes after safe_).
    static const unsigned char safe_[40];
    static const unsigned long limit_[37];
};

}  // namespace rts6x

#endif
