// Spec: ISO C 7.12.1 (domain, pole and range errors: errno EDOM or ERANGE, math_errhandling
// MATH_ERRNO) and F.9 (the values returned): a NaN, an infinity of the sign asked, or a zero.
#ifndef RTS6X_MATH_ERROR_H
#define RTS6X_MATH_ERROR_H

#include <errno.h>
#include "../misc/ErrorNumber.h"
#include "MathBits.h"

namespace rts6x {

class MathError {
public:
    // A domain error: EDOM, and the default NaN.
    static double domain() { ErrorNumber::set(EDOM); return MathBits::notANumber(); }
    // A pole error or an overflow: ERANGE, and HUGE_VAL of the sign asked.
    static double overflow(bool negative) { ErrorNumber::set(ERANGE); return MathBits::infinity(negative); }
    // An underflow to zero: ERANGE, and a zero of the sign asked.
    static double underflow(bool negative) { ErrorNumber::set(ERANGE); return MathBits::zero(negative); }
    static void range() { ErrorNumber::set(ERANGE); }
};

}  // namespace rts6x

#endif
