// Spec: the constants of ISO C99 7.12.5, 7.12.6 and 7.12.8's functions, double-doubles worked out to 90 digits.
#ifndef RTS6X_EXTENDED_CONSTANTS_H
#define RTS6X_EXTENDED_CONSTANTS_H

#include "DoubleDouble.h"
#include "MathBits.h"

namespace rts6x {

class ExtendedConstants {
public:
    static DoubleDouble of(unsigned long long hi, unsigned long long lo) { return DoubleDouble(MathBits::from(hi), MathBits::from(lo)); }
    static DoubleDouble pi() { return of(0x400921FB54442D18ULL, 0x3CA1A62633145C07ULL); }
    static DoubleDouble lnTwo() { return of(0x3FE62E42FEFA39EFULL, 0x3C7ABC9E3B39803FULL); }
    static DoubleDouble lnPi() { return of(0x3FF250D048E7A1BDULL, 0x3C67ABF2AD8D5088ULL); }
    static DoubleDouble twoOverRootPi() { return of(0x3FF20DD750429B6DULL, 0x3C71AE3A914FED80ULL); }
    static DoubleDouble oneOverRootPi() { return of(0x3FE20DD750429B6DULL, 0x3C61AE3A914FED80ULL); }
    static DoubleDouble halfLnTwoPi() { return of(0x3FED67F1C864BEB5ULL, 0xBC865B5A1B7FF5DFULL); }
    static DoubleDouble eulerGamma() { return of(0x3FE2788CFC6FB619ULL, 0xBC56CB90701FBFABULL); }
    static DoubleDouble oneLessGamma() { return of(0x3FDB0EE6072093CEULL, 0x3C56CB90701FBFABULL); }
};

}  // namespace rts6x

#endif
