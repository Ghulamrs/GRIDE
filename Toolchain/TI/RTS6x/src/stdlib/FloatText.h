// Spec: ISO C 7.20.1.3 (strtod: decimal, hexadecimal, INF, INFINITY, NAN, NAN(...)) and F.5 (to
// binary correctly rounded, ties to even), for binary64 and binary32. Exact: the digits become an
// integer and the division by a power of ten is carried out in full.
#ifndef RTS6X_FLOAT_TEXT_H
#define RTS6X_FLOAT_TEXT_H

#include "../helpers/SoftFloat.h"

namespace rts6x {

class BigNumber;

class FloatText {
public:
    // The number s begins with; end where it stopped (s if none); range set on inexact underflow.
    static unsigned long long read(const FloatFormat &format, const char *s, const char **end, bool &range);
    // strtod's way in: a plain decimal of <= 19 significant digits, decided short; else false, read().
    static bool quick(const char *s, const char **end, unsigned long long &bits);

private:
    // Kept digits: as many as any binary64 value needs exactly, plus the ones that decide a tie.
    enum { MaxDigits = 780 };

    static bool matchWord(const char *s, const char *word);
    // NAN(n-char-sequence): the sequence read as an integer, as strtoull with base 0 would, or 0.
    static unsigned long long nanPayload(const char *s, const char *stop);
    static unsigned long long special(const FloatFormat &format, bool negative, const char *s, const char **end);
    static unsigned long long hexadecimal(const FloatFormat &format, bool negative, const char *s,
                                          const char **end, bool &range);
    static unsigned long long decimal(const FloatFormat &format, bool negative, const char *s,
                                      const char **end, bool &range);
    // An exponent part "e+12" at p, added to exponent; where the number ends.
    static const char *exponentPart(const char *p, int &exponent);
    // w x 10^exponent when one correctly rounded operation gives it exactly (Clinger 1990): w and
    // 10^|exponent| both exact in the format; false where the long way is needed.
    static bool shortWay(const FloatFormat &format, bool negative, unsigned long long w, int exponent,
                         unsigned long long &bits);
    static bool shortWay64(bool negative, unsigned long long w, int exponent, unsigned long long &bits);
    // digits x 10^exponent, the digits' integer already in d, sticky for any nonzero digit dropped.
    static unsigned long long scale(const FloatFormat &format, bool negative, BigNumber &d, int count,
                                    int exponent, bool sticky, bool &range);
    static int hexValue(char c);
};

}  // namespace rts6x

#endif
