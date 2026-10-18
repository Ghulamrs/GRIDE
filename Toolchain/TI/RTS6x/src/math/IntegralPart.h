// Spec: ISO C 7.12.9.2 ceil, 7.12.9.4 floor, 7.12.9.6 round, 7.12.9.8 trunc, 7.12.6.12 modf, and
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
    // modf: x's integral part to *whole, the signed fraction returned.
    static double split(double x, double *whole);

private:
    // x's bits rounded to an integer toward -inf (up false) or +inf (up true).
    static unsigned long long directed(unsigned long long u, bool up);
};

}  // namespace rts6x

#endif
