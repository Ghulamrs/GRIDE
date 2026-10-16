// Spec: ISO C 7.12.7.4 pow and F.9.4.4: x^y = e^(y ln x), ln x as a double-double, y ln x by
// Dekker's exact product, e^z by ExpKernel - so the result is within an ulp over the whole range;
// a negative x takes an integer y, its parity giving the sign.
#ifndef RTS6X_POWER_H
#define RTS6X_POWER_H

namespace rts6x {

class Power {
public:
    // pow: EDOM for x < 0 and y not an integer; ERANGE for x = 0 and y < 0, and past either end.
    static double raise(double x, double y);

private:
    enum Parity { NotInteger, Odd, Even };
    static Parity parity(unsigned long long y);
    // x^y negated if asked, for x > 0 and y with no special value left: x, y finite, neither zero.
    static double finite(double x, double y, bool negative);
};

}  // namespace rts6x

#endif
