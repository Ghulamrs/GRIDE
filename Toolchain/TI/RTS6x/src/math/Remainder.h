// Spec: ISO C 7.12.10.1 fmod and F.9.7.1: x - n*y, n the quotient truncated, with x's sign and
// |result| < |y|; exact, since every such remainder is a double. Integer long division on the
// significands, eleven bits at a step.
#ifndef RTS6X_REMAINDER_H
#define RTS6X_REMAINDER_H

namespace rts6x {

class Remainder {
public:
    // fmod: a domain error (EDOM, NaN) for an infinite x or a zero y.
    static double truncated(double x, double y);
};

}  // namespace rts6x

#endif
