// Spec: ISO C 7.12.9.2 ceil, 7.12.9.4 floor, 7.12.9.6 round, 7.12.9.8 trunc, 7.12.6.12 modf, C99 7.12.9.3
// nearbyint, 7.12.9.4 rint, 7.12.9.5 lrint, 7.12.9.7 lround, and
// F.9.6: integral values in binary64, exact - the fraction bits below the units place cleared or
// carried into; zeros keep their sign, infinities and NaNs return themselves.
#ifndef RTS6X_INTEGRAL_PART_H
#define RTS6X_INTEGRAL_PART_H

namespace rts6x {

class IntegralPart {
public:
    static double down(double x);           // floor
    static double up(double x);             // ceil
    static double towardZero(double x);     // trunc
    static double nearestAway(double x);    // round: halfway cases away from zero
    static double nearestEven(double x);    // rint: halfway cases to even
    // An integral value r, or a NaN or an infinity, as the type: outside it 0 and EDOM (lrint, lround, llrint, llround).
    static long toLong(double r);
    static long long toLongLong(double r);
    // modf: x's integral part to *whole, the signed fraction returned.
    static double split(double x, double *whole);

private:
    // x's bits rounded to an integer toward -inf (up false) or +inf (up true).
    static unsigned long long directed(unsigned long long u, bool up);
};

}  // namespace rts6x

#endif
