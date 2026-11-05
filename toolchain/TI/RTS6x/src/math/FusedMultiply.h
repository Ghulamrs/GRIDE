// Spec: ISO C99 7.12.13.1 fma and F.9.10.1: x * y + z as if with unbounded range and precision, rounded once to
// nearest, ties to even, on the integers alone.
#ifndef RTS6X_FUSED_MULTIPLY_H
#define RTS6X_FUSED_MULTIPLY_H

namespace rts6x {

class FusedMultiply {
public:
    static double evaluate(double x, double y, double z);
};

}  // namespace rts6x

#endif
